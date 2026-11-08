// The title-agnostic sight layer (see sight_layer.h). Every publication is a seqlock with one writer
// thread per title: the ray from the aim path, the trace result from the title's simulation hook.

#include "sight_layer.h"
#include "title_adapter.h"
#include "../common/aim_lead_logic.h"
#include "../common/session_read_logic.h"
#include "../common/aim_solve_logic.h"
#include "../common/config.h"
#include "../common/log.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
constexpr size_t kTitleSlots = 8;

struct RayPublication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> sampleMs{0};
    std::atomic<float> head[3]{};
    std::atomic<float> dir[3]{};
    std::atomic<float> eye[3]{};
    std::atomic<int32_t> unit{-1}, ignoreB{-1}, ignoreC{-1};
    std::atomic<float> eyeVel[3]{};
    std::atomic<bool> eyeVelValid{false};
    std::atomic<uint64_t> eyeStampUs{0};
};

struct TracePublication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> sampleMs{0};
    std::atomic<uint32_t> hit{0};
    std::atomic<float> rangeWu{0.0f};
    std::atomic<float> convergeRangeWu{0.0f};
    std::atomic<bool> faulted{false};
    std::atomic<uint64_t> lastTraceMs{0};
    // The inverse-range average, owned by the title's trace thread (Sight_ResetTitle runs at install,
    // before that thread traces).
    std::atomic<float> invRangePerWu{0.0f};
    std::atomic<uint64_t> smoothMs{0};
    std::atomic<uint32_t> smoothGeneration{0};
    // Which ray the trace thread is tracing (Sight_SetStance): 0 on foot, a seat's key otherwise.
    std::atomic<uint32_t> stance{0};
};

struct ConvergedStamp
{
    std::atomic<uint64_t> atMs{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<float> unitsPerVrMetre{0.0f};
    // Sight_MarkAimWritten: the converged aim is the game's aim itself, not a servo's target.
    std::atomic<uint64_t> writtenMs{0};
    std::atomic<uint32_t> writtenGeneration{0};
};

constexpr int kSessionUnread = -2;   // no simulation thread has reported a session type yet

struct Stats
{
    std::atomic<uint64_t> rays{0}, traces{0}, hits{0}, misses{0}, faults{0}, converged{0}, declined{0}, written{0};
    std::atomic<uint64_t> costTicks{0}, costMaxTicks{0};
    // Shots from the muzzle: aimed through the converged point, refused (by reason: aim_solve::MuzzleShot), or
    // without a point; and (defensive) held off because the gun points elsewhere, naturally -- a seat held at its
    // stop, a turret.
    std::atomic<uint64_t> muzzleAimed{0}, muzzleUnconverged{0}, muzzleHeld{0};
    // Local shots a fire-time change was stood down for (coop_safe_shots).
    std::atomic<uint64_t> fireTimeStoodDown{0};
    // The session as the game holds it (Sight_NoteSession; the type is kSessionUnread until a simulation thread
    // reports), and what the report last logged (the reporting thread's own).
    std::atomic<int> sessionType{kSessionUnread}, sessionPlayers{-1}, sessionOthers{-1};
    int sessionTypeLogged = kSessionUnread, sessionPlayersLogged = -1, sessionOthersLogged = -1;
    // Of those, the re-aims that would have applied, and by how much (milli-degrees): what the stand-down costs.
    // [0] on foot, [1] seated; `through`: the re-aims that would have left the shot alone (already through the point).
    struct StoodDownBends
    {
        std::atomic<uint64_t> count{0}, sumMilliDeg{0}, through{0};
        std::atomic<uint32_t> maxMilliDeg{0};
    } fireTimeBends[2];
    std::atomic<uint64_t> muzzleRefused[static_cast<size_t>(aim_solve::MuzzleShot::Count)]{};
};

// THE WRITTEN AIM'S LEAD, per title (Sight_LeadWrittenAim). The filter and `seenEpoch` belong to the title's
// simulation thread; the counters are one report window's, swapped out by the cold worker.
struct LeadLane
{
    aim_lead::Filter filter;
    uint32_t seenEpoch = 0;
    std::atomic<uint32_t> epoch{0};   // Sight_ResetTitle: a new title generation starts afresh
    std::atomic<uint64_t> outcomes[static_cast<size_t>(aim_lead::Outcome::Count)]{};
    std::atomic<uint64_t> restarts[static_cast<size_t>(aim_lead::Restart::Count)]{};
    std::atomic<uint64_t> capped{0}, repeated{0}, drawn{0}, leadSumMicroDeg{0};
    std::atomic<uint32_t> leadMaxMilliDeg{0}, leadUs{0};
};

// The automatic co-op switch's gate (session_read_logic.h GateNote / GateOpen): stepped by the noting title's
// simulation thread, read by every thread that asks Sight_FireTimeChangesAllowed. One gate: one title runs at a
// time, and a note from another title starts the run again.
struct SessionGate
{
    std::atomic<uint32_t> safeNotes{0};
    std::atomic<uint64_t> lastNoteMs{0};
    std::atomic<uint8_t> title{0xFF}, standing{0};
    // What the report last logged (the reporting thread's own).
    int loggedOpen = -1;
    uint8_t loggedStanding = 0xFF;
};
SessionGate g_sessionGate;

RayPublication g_rays[kTitleSlots];
// The engine eye's track per title, owned by the title's ray publisher (one aim thread per title; the try-lock only
// keeps a second publisher, should one ever appear, from tearing it -- that publish then carries no velocity).
shot_lag::EyeTrack g_eyeTracks[kTitleSlots];
std::atomic_flag g_eyeTrackBusy[kTitleSlots] = {};
TracePublication g_traces[kTitleSlots];
ConvergedStamp g_converged[kTitleSlots];
Stats g_stats[kTitleSlots];
LeadLane g_leads[kTitleSlots];

size_t Slot(GameTitle title) { return static_cast<size_t>(title) % kTitleSlots; }

bool Fresh(uint64_t at, uint64_t now, uint64_t maxAge) { return at && now >= at && now - at <= maxAge; }

// A seqlock over a trivially copyable value, every word atomic (the committed-aim-sample discipline): one writer per
// slot, any reader; a torn read retries once, then reports nothing.
template <class T>
struct SeqBlob
{
    static constexpr size_t kWords = (sizeof(T) + 7) / 8;
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint64_t> words[kWords]{};
    void Publish(const T& value)
    {
        uint64_t raw[kWords]{};
        memcpy(raw, &value, sizeof(T));
        sequence.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_release);
        for (size_t i = 0; i < kWords; ++i)
            words[i].store(raw[i], std::memory_order_relaxed);
        sequence.fetch_add(1, std::memory_order_release);
    }
    bool Read(T& out) const
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            uint64_t raw[kWords]{};
            for (size_t i = 0; i < kWords; ++i)
                raw[i] = words[i].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence.load(std::memory_order_relaxed) != before)
                continue;
            memcpy(&out, raw, sizeof(T));
            return true;
        }
        return false;
    }
};

const char* TitleName(GameTitle title)
{
    switch (title)
    {
    case GameTitle::Halo3: return "Halo 3";
    case GameTitle::Halo3ODST: return "ODST";
    case GameTitle::HaloReach: return "Reach";
    case GameTitle::Halo4: return "Halo 4";
    case GameTitle::HaloCE: return "Halo CE";
    case GameTitle::Halo2: return "Halo 2";
    default: return "?";
    }
}
} // namespace

bool Sight_AimEnabled()
{
    return g_config.converged_aim != 0;
}

uint64_t Sight_NowUs()
{
    static const int64_t s_frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const int64_t ticks = now.QuadPart;
    // Split so ticks * 1e6 cannot overflow over a long uptime.
    return static_cast<uint64_t>(ticks / s_frequency) * 1000000ull +
           static_cast<uint64_t>((ticks % s_frequency) * 1000000ll / s_frequency);
}

uint64_t Sight_NowMs()
{
    return Sight_NowUs() / 1000ull;
}

void Sight_PublishRay(GameTitle title, const SightRay& rayIn)
{
    if (!Sight_AimEnabled())
        return;
    SightRay ray = rayIn;
    if (!ray.eyeVelValid)
    {
        // The eye's velocity over ~100 ms, on this (the publishing) thread, on the eye's own clock where the
        // publisher knows it.
        const size_t slot = Slot(title);
        if (!g_eyeTrackBusy[slot].test_and_set(std::memory_order_acquire))
        {
            auto& track = g_eyeTracks[slot];
            static uint32_t s_trackGeneration[kTitleSlots]{};
            if (s_trackGeneration[slot] != ray.generation)
            {
                track.Reset();
                s_trackGeneration[slot] = ray.generation;
            }
            track.Push(ray.eyeStampUs ? ray.eyeStampUs : Sight_NowUs(), ray.eye);
            ray.eyeVelValid = track.Velocity(120000, 30000, ray.eyeVel);
            g_eyeTrackBusy[slot].clear(std::memory_order_release);
        }
    }
    auto& p = g_rays[Slot(title)];
    p.sequence.fetch_add(1, std::memory_order_acq_rel);   // odd: a publish is in progress
    p.generation.store(ray.generation, std::memory_order_relaxed);
    // Stamped HERE, on the one sight clock its readers age it against (Sight_ReadRay): a caller's GetTickCount64
    // stamp would put its 15.6 ms steps and its offset from QPC into every freshness window.
    p.sampleMs.store(Sight_NowMs(), std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
    {
        p.head[i].store(ray.head[i], std::memory_order_relaxed);
        p.dir[i].store(ray.dir[i], std::memory_order_relaxed);
        p.eye[i].store(ray.eye[i], std::memory_order_relaxed);
    }
    p.unit.store(ray.unit, std::memory_order_relaxed);
    p.ignoreB.store(ray.ignoreB, std::memory_order_relaxed);
    p.ignoreC.store(ray.ignoreC, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
        p.eyeVel[i].store(ray.eyeVelValid ? ray.eyeVel[i] : 0.0f, std::memory_order_relaxed);
    p.eyeVelValid.store(ray.eyeVelValid, std::memory_order_relaxed);
    p.eyeStampUs.store(ray.eyeStampUs, std::memory_order_relaxed);
    p.sequence.fetch_add(1, std::memory_order_release);
    g_stats[Slot(title)].rays.fetch_add(1, std::memory_order_relaxed);
}

bool Sight_ReadRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightRay& out)
{
    const auto& p = g_rays[Slot(title)];
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = p.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        SightRay r{};
        r.generation = p.generation.load(std::memory_order_relaxed);
        r.sampleMs = p.sampleMs.load(std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i)
        {
            r.head[i] = p.head[i].load(std::memory_order_relaxed);
            r.dir[i] = p.dir[i].load(std::memory_order_relaxed);
            r.eye[i] = p.eye[i].load(std::memory_order_relaxed);
        }
        r.unit = p.unit.load(std::memory_order_relaxed);
        r.ignoreB = p.ignoreB.load(std::memory_order_relaxed);
        r.ignoreC = p.ignoreC.load(std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i)
            r.eyeVel[i] = p.eyeVel[i].load(std::memory_order_relaxed);
        r.eyeVelValid = p.eyeVelValid.load(std::memory_order_relaxed);
        r.eyeStampUs = p.eyeStampUs.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (p.sequence.load(std::memory_order_relaxed) != before)
            continue;
        if (!generation || r.generation != generation || !Fresh(r.sampleMs, Sight_NowMs(), maxAgeMs))
            return false;
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(r.head[i]) || !std::isfinite(r.dir[i]) || !std::isfinite(r.eye[i]))
                return false;
        out = r;
        return true;
    }
    return false;
}

bool Sight_TraceDue(GameTitle title, uint64_t nowMs)
{
    if (!Sight_AimEnabled())
        return false;
    auto& t = g_traces[Slot(title)];
    const uint64_t last = t.lastTraceMs.load(std::memory_order_relaxed);
    if (last && nowMs >= last && nowMs - last < kSightTraceIntervalMs)
        return false;
    t.lastTraceMs.store(nowMs, std::memory_order_relaxed);
    return true;
}

void Sight_PublishTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu, uint64_t costTicks)
{
    auto& t = g_traces[Slot(title)];
    const uint64_t now = Sight_NowMs();
    // The range the aim converges on (SightSmoothInverseRange): the inverse range averaged over 120 ms, a
    // miss counting as the trace's far end. A new generation, or a gap in the traces (a seat, a menu, a
    // stall), starts afresh from this trace.
    const uint64_t lastMs = t.smoothMs.load(std::memory_order_relaxed);
    const bool continuous = lastMs && now >= lastMs && now - lastMs <= kSightFreshMs &&
        t.smoothGeneration.load(std::memory_order_relaxed) == generation;
    const float dt = continuous ? float(now - lastMs) / 1000.0f : float(kSightTraceIntervalMs) / 1000.0f;
    const float inv = SightSmoothInverseRange(t.invRangePerWu.load(std::memory_order_relaxed), continuous,
        hit, rangeWu, dt);
    t.invRangePerWu.store(inv, std::memory_order_relaxed);
    t.smoothMs.store(now, std::memory_order_relaxed);
    t.smoothGeneration.store(generation, std::memory_order_relaxed);
    t.sequence.fetch_add(1, std::memory_order_acq_rel);
    t.generation.store(generation, std::memory_order_relaxed);
    t.sampleMs.store(now, std::memory_order_relaxed);
    t.hit.store(hit ? 1u : 0u, std::memory_order_relaxed);
    t.rangeWu.store(rangeWu, std::memory_order_relaxed);
    t.convergeRangeWu.store(inv > 0.0f ? 1.0f / inv : 0.0f, std::memory_order_relaxed);
    t.sequence.fetch_add(1, std::memory_order_release);
    auto& s = g_stats[Slot(title)];
    s.traces.fetch_add(1, std::memory_order_relaxed);
    (hit ? s.hits : s.misses).fetch_add(1, std::memory_order_relaxed);
    s.costTicks.fetch_add(costTicks, std::memory_order_relaxed);
    uint64_t prior = s.costMaxTicks.load(std::memory_order_relaxed);
    while (costTicks > prior && !s.costMaxTicks.compare_exchange_weak(prior, costTicks, std::memory_order_relaxed)) {}
}

bool Sight_ReadTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightTrace& out)
{
    const auto& t = g_traces[Slot(title)];
    if (t.faulted.load(std::memory_order_acquire))
        return false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = t.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        SightTrace r{};
        r.generation = t.generation.load(std::memory_order_relaxed);
        r.sampleMs = t.sampleMs.load(std::memory_order_relaxed);
        r.hit = t.hit.load(std::memory_order_relaxed) != 0;
        r.rangeWu = t.rangeWu.load(std::memory_order_relaxed);
        r.convergeRangeWu = t.convergeRangeWu.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (t.sequence.load(std::memory_order_relaxed) != before)
            continue;
        if (!generation || r.generation != generation || !Fresh(r.sampleMs, Sight_NowMs(), maxAgeMs))
            return false;
        r.hit = r.hit && std::isfinite(r.rangeWu) && r.rangeWu > 0.0f;
        // A miss means the target is at least the whole trace away (the reticle parks); the aim's
        // smoothed range (convergeRangeWu) is heading for that far end too.
        if (!r.hit)
            r.rangeWu = kSightTraceLengthWu;
        if (!std::isfinite(r.convergeRangeWu) || !(r.convergeRangeWu > 0.0f))
            r.convergeRangeWu = 0.0f;
        out = r;
        return true;
    }
    return false;
}

void Sight_NoteTraceFault(GameTitle title)
{
    g_traces[Slot(title)].faulted.store(true, std::memory_order_release);
    g_stats[Slot(title)].faults.fetch_add(1, std::memory_order_relaxed);
}

bool Sight_TraceFaulted(GameTitle title)
{
    return g_traces[Slot(title)].faulted.load(std::memory_order_acquire);
}

void Sight_ResetTitle(GameTitle title)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    // [assess-only begin]
    Sight_ResetAssessTrace(title);   // the assessment trace's own latch, on the same reinstall
    // [assess-only end]
#endif
    auto& t = g_traces[Slot(title)];
    t.faulted.store(false, std::memory_order_release);
    t.lastTraceMs.store(0, std::memory_order_relaxed);
    t.invRangePerWu.store(0.0f, std::memory_order_relaxed);
    t.smoothMs.store(0, std::memory_order_relaxed);
    t.stance.store(0, std::memory_order_relaxed);
    g_converged[Slot(title)].atMs.store(0, std::memory_order_release);
    g_converged[Slot(title)].writtenMs.store(0, std::memory_order_release);
    g_leads[Slot(title)].epoch.fetch_add(1, std::memory_order_release);
}

void Sight_SetStance(GameTitle title, uint32_t stance)
{
    auto& t = g_traces[Slot(title)];
    if (t.stance.load(std::memory_order_relaxed) == stance)
        return;
    // Another stance (the trace thread only): the published result and the smoothed range are the old stance's.
    // The result is withdrawn (a read finds nothing fresh until this stance's first trace, one call away) and the
    // average starts afresh.
    t.sequence.fetch_add(1, std::memory_order_acq_rel);
    t.sampleMs.store(0, std::memory_order_relaxed);
    t.hit.store(0u, std::memory_order_relaxed);
    t.rangeWu.store(0.0f, std::memory_order_relaxed);
    t.convergeRangeWu.store(0.0f, std::memory_order_relaxed);
    t.sequence.fetch_add(1, std::memory_order_release);
    t.invRangePerWu.store(0.0f, std::memory_order_relaxed);
    t.smoothMs.store(0, std::memory_order_relaxed);
    t.stance.store(stance, std::memory_order_release);
}

uint32_t Sight_Stance(GameTitle title)
{
    return g_traces[Slot(title)].stance.load(std::memory_order_acquire);
}

void Sight_MarkConverged(GameTitle title, float unitsPerVrMetre)
{
    auto& c = g_converged[Slot(title)];
    c.generation.store(TitleAdapter_GetGeneration(title), std::memory_order_relaxed);
    c.unitsPerVrMetre.store(unitsPerVrMetre, std::memory_order_relaxed);
    c.atMs.store(Sight_NowMs(), std::memory_order_release);
    g_stats[Slot(title)].converged.fetch_add(1, std::memory_order_relaxed);
}

float Sight_UnitsPerVrMetre(GameTitle title)
{
    return g_converged[Slot(title)].unitsPerVrMetre.load(std::memory_order_relaxed);
}

bool Sight_ConvergedLive(GameTitle title)
{
    const auto& c = g_converged[Slot(title)];
    const uint64_t at = c.atMs.load(std::memory_order_acquire);
    return Fresh(at, Sight_NowMs(), 100) &&
           c.generation.load(std::memory_order_relaxed) == TitleAdapter_GetGeneration(title);
}

void Sight_MarkAimWritten(GameTitle title)
{
    auto& c = g_converged[Slot(title)];
    c.writtenGeneration.store(TitleAdapter_GetGeneration(title), std::memory_order_relaxed);
    c.writtenMs.store(Sight_NowMs(), std::memory_order_release);
    g_stats[Slot(title)].written.fetch_add(1, std::memory_order_relaxed);
}

bool Sight_AimWrittenLive(GameTitle title)
{
    const auto& c = g_converged[Slot(title)];
    const uint64_t at = c.writtenMs.load(std::memory_order_acquire);
    return Fresh(at, Sight_NowMs(), 100) &&
           c.writtenGeneration.load(std::memory_order_relaxed) == TitleAdapter_GetGeneration(title);
}

bool Sight_ConvergedDirection(GameTitle title, uint32_t generation, const float head[3], const float dir[3],
                              const float eye[3], float out[3], float maxPitchDeg, float* outRangeWu,
                              bool* outHit, converge::ConvergeOff* outWhy, aim_solve::RailHold* railHold)
{
    using converge::ConvergeOff;
    const auto decline = [&](ConvergeOff why) {
        if (outWhy)
            *outWhy = why;
        if (railHold)
            railHold->reset();
        return false;
    };
    SightTrace trace{};
    if (!Sight_AimEnabled())
        return decline(ConvergeOff::AimOff);
    if (!head || !dir || !eye || !out)
        return decline(ConvergeOff::RayStale);
    if (!Sight_ReadTrace(title, generation, kSightFreshMs, trace))
        return decline(Sight_TraceFaulted(title) ? ConvergeOff::TraceFaulted : ConvergeOff::TraceStale);
    if (!(trace.convergeRangeWu > 0.0f))   // no hit yet since the traces (re)started: nothing to converge on
    {
        g_stats[Slot(title)].declined.fetch_add(1, std::memory_order_relaxed);
        return decline(ConvergeOff::NoRange);
    }
    aim_solve::ConvergeRails rails{};
    rails.max_pitch_deg = maxPitchDeg;
    aim_solve::Vec3 bent{};
    if (!aim_solve::converge_direction(aim_solve::Vec3{head[0] - eye[0], head[1] - eye[1], head[2] - eye[2]},
                                       aim_solve::Vec3{dir[0], dir[1], dir[2]}, trace.convergeRangeWu,
                                       kSightCmPerWorldUnit, rails, bent, railHold))
    {
        // The geometry did not resolve: a point inside the range floor, or a degenerate ray. (A steep bend does
        // not decline: Halo 3 / ODST / Halo 4 hold it at the rail and stay converged.)
        g_stats[Slot(title)].declined.fetch_add(1, std::memory_order_relaxed);
        return decline(ConvergeOff::NoRange);
    }
    out[0] = bent.x;
    out[1] = bent.y;
    out[2] = bent.z;
    if (outRangeWu) *outRangeWu = trace.convergeRangeWu;
    if (outHit) *outHit = trace.hit;
    return true;
}

void Sight_NoteSession(GameTitle title, int type, int players, int otherMachines)
{
    auto& s = g_stats[Slot(title)];
    s.sessionType.store(type, std::memory_order_relaxed);
    s.sessionPlayers.store(players, std::memory_order_relaxed);
    s.sessionOthers.store(otherMachines, std::memory_order_relaxed);
    // The automatic switch's gate: one more note.
    session_read::Session session{};
    session.type = type;
    session.players = players;
    session.others = otherMachines;
    const session_read::Standing standing = session_read::Classify(session);
    auto& g = g_sessionGate;
    const uint64_t now = Sight_NowMs();
    const uint64_t last = g.lastNoteMs.load(std::memory_order_relaxed);
    const bool continued = last && now >= last && now - last <= session_read::kGateStaleMs &&
                           g.title.load(std::memory_order_relaxed) == static_cast<uint8_t>(title);
    g.safeNotes.store(session_read::GateNote(g.safeNotes.load(std::memory_order_relaxed),
                                             session_read::OneMachineChangesSafe(standing), continued),
                      std::memory_order_release);
    g.title.store(static_cast<uint8_t>(title), std::memory_order_relaxed);
    g.standing.store(static_cast<uint8_t>(standing), std::memory_order_relaxed);
    g.lastNoteMs.store(now, std::memory_order_release);
}

int Sight_SessionType(GameTitle title)
{
    return g_stats[Slot(title)].sessionType.load(std::memory_order_relaxed);
}

bool Sight_FireTimeChangesAllowed()
{
    const int mode = g_config.coop_safe_shots;
    if (mode == 0)
        return true;
    if (mode != 2)
        return false;
    // Automatic: only while the gate is open. Notes that have stopped coming close it.
    const uint64_t last = g_sessionGate.lastNoteMs.load(std::memory_order_acquire);
    const uint64_t now = Sight_NowMs();
    return session_read::GateOpen(g_sessionGate.safeNotes.load(std::memory_order_acquire),
                                  last && now >= last ? now - last : ~0ull);
}

void Sight_NoteFireTimeStoodDown(GameTitle title)
{
    g_stats[Slot(title)].fireTimeStoodDown.fetch_add(1, std::memory_order_relaxed);
}

bool Sight_AimLeadOn()
{
    return aim_lead::ClampTicks(g_config.aim_lead_ticks, 0.0f) > 0.0f;
}

void Sight_ForgetAimLead(GameTitle title)
{
    g_leads[Slot(title)].filter.Reset();
}

void Sight_LeadWrittenAim(GameTitle title, uint64_t sampleKey, uint32_t tickUs, bool drawnFromRecord, float& yaw,
                          float& pitch)
{
    auto& lane = g_leads[Slot(title)];
    const uint32_t epoch = lane.epoch.load(std::memory_order_acquire);
    if (epoch != lane.seenEpoch)
    {
        lane.seenEpoch = epoch;
        lane.filter.Reset();
    }
    const float leadS = aim_lead::LeadSeconds(g_config.aim_lead_ticks, tickUs);
    if (!(leadS > 0.0f))
    {
        lane.filter.Reset();
        return;
    }
    if (drawnFromRecord)
    {
        // Something drawn looks along the record: it is written as the hand's, and the motion so far is forgotten
        // (the lead starts again from its third sample once nothing does).
        lane.filter.Reset();
        lane.drawn.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const aim_lead::Result r = lane.filter.Step(Sight_NowUs(), sampleKey, yaw, pitch, leadS);
    lane.leadUs.store(static_cast<uint32_t>(leadS * 1.0e6f + 0.5f), std::memory_order_relaxed);
    lane.outcomes[static_cast<size_t>(r.outcome) % static_cast<size_t>(aim_lead::Outcome::Count)].fetch_add(
        1, std::memory_order_relaxed);
    lane.restarts[static_cast<size_t>(r.restart) % static_cast<size_t>(aim_lead::Restart::Count)].fetch_add(
        1, std::memory_order_relaxed);
    if (r.repeated)
        lane.repeated.fetch_add(1, std::memory_order_relaxed);
    if (r.outcome != aim_lead::Outcome::Led)
        return;
    if (r.capped)
        lane.capped.fetch_add(1, std::memory_order_relaxed);
    const float leadDeg = r.leadRad * aim_lead::kRadToDeg;
    lane.leadSumMicroDeg.fetch_add(static_cast<uint64_t>(leadDeg * 1.0e6f), std::memory_order_relaxed);
    const uint32_t milli = static_cast<uint32_t>(leadDeg * 1000.0f);
    uint32_t prior = lane.leadMaxMilliDeg.load(std::memory_order_relaxed);
    while (milli > prior && !lane.leadMaxMilliDeg.compare_exchange_weak(prior, milli, std::memory_order_relaxed)) {}
    yaw = r.yaw;
    pitch = r.pitch;
}

bool Sight_ShotsFromMuzzle()
{
    return g_config.shots_from_muzzle && Sight_FireTimeChangesAllowed();
}

bool Sight_GunBarrelAim()
{
    return g_config.gun_barrel_aim && Sight_FireTimeChangesAllowed();
}

bool Sight_IndependentDualAim()
{
    return g_config.independent_dual_aim && Sight_FireTimeChangesAllowed();
}

bool Sight_PhysicalMelee()
{
    return g_config.physical_melee && Sight_FireTimeChangesAllowed();
}

bool Sight_MuzzleShotsConverge()
{
    return Sight_ShotsFromMuzzle() && !Sight_GunBarrelAim() && Sight_AimEnabled();
}

void Sight_NoteMuzzleShotUnconverged(GameTitle title)
{
    g_stats[Slot(title)].muzzleUnconverged.fetch_add(1, std::memory_order_relaxed);
}

void Sight_NoteMuzzleShotHeld(GameTitle title)
{
    g_stats[Slot(title)].muzzleHeld.fetch_add(1, std::memory_order_relaxed);
}

bool Sight_AimMuzzleShotThrough(GameTitle title, const float point[3], const float rayDir[3],
                                const float before[3], const float position[3], float direction[3], float up[3])
{
    auto& refused = g_stats[Slot(title)].muzzleRefused;
    if (!Sight_FireTimeChangesAllowed())
    {
        Sight_NoteFireTimeStoodDown(title);
        return false;
    }
    if (!point || !rayDir || !before || !position || !direction || !up)
    {
        refused[static_cast<size_t>(aim_solve::MuzzleShot::NonFinite)].fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    aim_solve::Vec3 out{}, newUp{up[0], up[1], up[2]};
    const aim_solve::MuzzleShot why = aim_solve::muzzle_shot_direction(
        aim_solve::Vec3{before[0], before[1], before[2]}, aim_solve::Vec3{position[0], position[1], position[2]},
        aim_solve::Vec3{point[0], point[1], point[2]}, aim_solve::Vec3{rayDir[0], rayDir[1], rayDir[2]},
        kSightCmPerWorldUnit, aim_solve::MuzzleShotRails{}, out, newUp);
    if (why != aim_solve::MuzzleShot::Aimed)
    {
        const size_t r = static_cast<size_t>(why);
        if (r < static_cast<size_t>(aim_solve::MuzzleShot::Count))
            refused[r].fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    direction[0] = out.x; direction[1] = out.y; direction[2] = out.z;
    up[0] = newUp.x; up[1] = newUp.y; up[2] = newUp.z;
    g_stats[Slot(title)].muzzleAimed.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool Sight_AimMuzzleShotFromRay(GameTitle title, uint32_t generation, const float head[3], const float dir[3],
                                const float eye[3], const float before[3], const float position[3],
                                float direction[3], float up[3], float* pointDistance,
                                const SightCameraSample* camera)
{
    // The point the converged aim in effect is bent through: the intent ray at the smoothed range the aim
    // path converged on (SightTrace::AimRangeWu), so the shot from the muzzle lands where the shot from the
    // engine eye would have.
    SightTrace trace{};
    if (!head || !dir || !Sight_ConvergedLive(title) || !Sight_ReadTrace(title, generation, kSightFreshMs, trace) ||
        !(trace.AimRangeWu() > 0.0f))
    {
        Sight_NoteMuzzleShotUnconverged(title);
        return false;
    }
    const float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!std::isfinite(length) || length < 1.0e-3f)
    {
        Sight_NoteMuzzleShotUnconverged(title);
        return false;
    }
    const float range = trace.AimRangeWu();
    float u[3]{}, point[3]{};
    for (int axis = 0; axis < 3; ++axis)
    {
        u[axis] = dir[axis] / length;
        point[axis] = head[axis] + u[axis] * range;
    }
    (void)eye;   // no distance-from-the-eye limit on the muzzle (aim_solve::muzzle_shot_direction)
    // FLAT PARITY (Halo 2, CE): the point moved with the eye from the camera sample it was built from to the fire.
    const float* through = point;
    float moved[3]{};
    if (camera)
    {
        SightPointSample sample{};
        memcpy(sample.point, point, sizeof(sample.point));
        memcpy(sample.dir, u, sizeof(sample.dir));
        sample.camera = *camera;
        through = Sight_ShotPointSample(title, SightTransportSite::Muzzle, sample, false, point, moved);
    }
    if (!Sight_AimMuzzleShotThrough(title, through, u, before, position, direction, up))
        return false;
    if (pointDistance)
    {
        float d2 = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            d2 += (through[axis] - position[axis]) * (through[axis] - position[axis]);
        *pointDistance = std::isfinite(d2) ? std::sqrt(d2) : 0.0f;
    }
    return true;
}

// ---- the intent point and the shot re-aims (shot_lag_logic.h) -------------------------------------------------
namespace
{
SeqBlob<SightIntent> g_intents[kTitleSlots];

struct ReaimStats
{
    std::atomic<uint64_t> applied{0}, through{0};
    std::atomic<uint64_t> refused[static_cast<size_t>(shot_lag::Reaim::Count)]{};
    std::atomic<uint64_t> skipped[static_cast<size_t>(shot_lag::IntentUse::Count)]{};
    std::atomic<uint32_t> bendSumMilliDeg{0}, bendMaxMilliDeg{0};
    // Seated queries (also in the totals above): the multiplayer residual (Sight_ReaimThroughPoint's `seated`).
    std::atomic<uint64_t> seatedApplied{0}, seatedThrough{0};
    std::atomic<uint32_t> seatedBendSumMilliDeg{0}, seatedBendMaxMilliDeg{0};
};
ReaimStats g_reaim[kTitleSlots][static_cast<size_t>(SightShotSite::Count)];

const char* SiteName(SightShotSite site)
{
    switch (site)
    {
    case SightShotSite::CrosshairAssist: return "crosshair-assist query";
    case SightShotSite::UnitAdjust: return "unit-adjust shot";
    case SightShotSite::Muzzle: return "muzzle shot";
    default: return "?";
    }
}
} // namespace

void Sight_PublishIntent(GameTitle title, const SightIntent& intent)
{
    if (!Sight_AimEnabled())
        return;
    g_intents[Slot(title)].Publish(intent);
}

bool Sight_ReadIntent(GameTitle title, SightIntent& out)
{
    return g_intents[Slot(title)].Read(out);
}

shot_lag::IntentUse Sight_IntentForShot(GameTitle title, uint64_t maxAgeMs, bool naturalHeld, SightIntent& out)
{
    out = SightIntent{};
    if (!Sight_AimEnabled())
        return shot_lag::IntentUse::NotConverged;
    const bool read = Sight_ReadIntent(title, out);
    return shot_lag::ShouldUseIntent(read, out.p, TitleAdapter_GetGeneration(title), Sight_NowUs(),
                                     maxAgeMs * 1000ull, Sight_ConvergedLive(title), naturalHeld);
}

bool Sight_ReaimThroughPoint(GameTitle title, SightShotSite site, const float start[3], float vector[3],
                             const float point[3], bool seated)
{
    const size_t s = static_cast<size_t>(site) % static_cast<size_t>(SightShotSite::Count);
    auto& stats = g_reaim[Slot(title)][s];
    float out[3]{};
    float bend = 0.0f;
    const shot_lag::Reaim why = shot_lag::ReaimThroughPoint(start, vector, point, shot_lag::ReaimRails{}, out, &bend);
    if (!Sight_FireTimeChangesAllowed())
    {
        // Stood down: `vector` stays as the game built it. The bend it would have had is kept for the log, on foot
        // apart from seated (the written aim's lead acts on foot only), with the shots that needed none.
        auto& stood = g_stats[Slot(title)];
        stood.fireTimeStoodDown.fetch_add(1, std::memory_order_relaxed);
        auto& bends = stood.fireTimeBends[seated ? 1 : 0];
        if (why == shot_lag::Reaim::Applied)
        {
            const uint32_t milli = static_cast<uint32_t>(std::min(bend * 1000.0f, 1.0e9f));
            bends.count.fetch_add(1, std::memory_order_relaxed);
            bends.sumMilliDeg.fetch_add(milli, std::memory_order_relaxed);
            uint32_t prior = bends.maxMilliDeg.load(std::memory_order_relaxed);
            while (milli > prior && !bends.maxMilliDeg.compare_exchange_weak(prior, milli, std::memory_order_relaxed)) {}
        }
        else if (why == shot_lag::Reaim::AlreadyThrough)
        {
            bends.through.fetch_add(1, std::memory_order_relaxed);
        }
        return false;
    }
    if (why == shot_lag::Reaim::AlreadyThrough)
    {
        stats.through.fetch_add(1, std::memory_order_relaxed);
        if (seated)
            stats.seatedThrough.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (why != shot_lag::Reaim::Applied)
    {
        stats.refused[static_cast<size_t>(why)].fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    vector[0] = out[0];
    vector[1] = out[1];
    vector[2] = out[2];
    stats.applied.fetch_add(1, std::memory_order_relaxed);
    const uint32_t milli = static_cast<uint32_t>(std::min(bend * 1000.0f, 1.0e9f));
    stats.bendSumMilliDeg.fetch_add(milli, std::memory_order_relaxed);
    uint32_t prior = stats.bendMaxMilliDeg.load(std::memory_order_relaxed);
    while (milli > prior && !stats.bendMaxMilliDeg.compare_exchange_weak(prior, milli, std::memory_order_relaxed)) {}
    if (seated)
    {
        stats.seatedApplied.fetch_add(1, std::memory_order_relaxed);
        stats.seatedBendSumMilliDeg.fetch_add(milli, std::memory_order_relaxed);
        uint32_t seatedPrior = stats.seatedBendMaxMilliDeg.load(std::memory_order_relaxed);
        while (milli > seatedPrior &&
               !stats.seatedBendMaxMilliDeg.compare_exchange_weak(seatedPrior, milli, std::memory_order_relaxed)) {}
    }
    return true;
}

void Sight_NoteReaimSkipped(GameTitle title, SightShotSite site, shot_lag::IntentUse why)
{
    const size_t s = static_cast<size_t>(site) % static_cast<size_t>(SightShotSite::Count);
    const size_t w = static_cast<size_t>(why);
    if (w < static_cast<size_t>(shot_lag::IntentUse::Count))
        g_reaim[Slot(title)][s].skipped[w].fetch_add(1, std::memory_order_relaxed);
}

// ---- the shot point moved with the eye (dev key shot_point_transport; shot_lag_logic.h 4a) ----------------------
namespace
{
std::atomic<SightFireEyeFn> g_fireEyeSources[kTitleSlots]{};
#if HALOMCCVR_DEV_DIAGNOSTICS
// One window per title and consumer: the fire thread takes the lock for one Add (plain sums), the cold worker to swap
// the window out.
struct TransportWitness
{
    SRWLOCK lock = SRWLOCK_INIT;
    shot_lag::TransportWindow window{};
};
TransportWitness g_transports[kTitleSlots][static_cast<size_t>(SightTransportSite::Count)];

const char* TransportSiteName(SightTransportSite site)
{
    switch (site)
    {
    case SightTransportSite::CrosshairAssist: return "crosshair-assist";
    case SightTransportSite::Muzzle: return "muzzle";
    case SightTransportSite::Record: return "record";
    case SightTransportSite::UnitAdjust: return "unit-adjust";
    default: return "?";
    }
}
#endif
} // namespace

void Sight_SetFireEyeSource(GameTitle title, SightFireEyeFn source)
{
    g_fireEyeSources[Slot(title)].store(source, std::memory_order_release);
}

bool Sight_TransportWanted(GameTitle title, bool seated)
{
    // Every build (flat parity): the keys are dev-only, so a player build keeps their defaults -- transported on
    // foot (shot_point_transport 1) and in a seat (shot_point_transport_seated 1). Every title whose local shots
    // aim through a world point; Reach on foot only (its seats are not covered by either key: sight_layer.h).
    if (title != GameTitle::Halo3 && title != GameTitle::Halo3ODST && title != GameTitle::Halo2 &&
        title != GameTitle::HaloCE && title != GameTitle::Halo4 && title != GameTitle::HaloReach)
        return false;
    if (title == GameTitle::HaloReach && seated)
        return false;
    return g_config.shot_point_transport != 0 && (!seated || g_config.shot_point_transport_seated != 0);
}

bool Sight_FireEyeAsked(GameTitle title)
{
    return Sight_TransportWanted(title, false) && Sight_FireTimeChangesAllowed();
}

namespace
{
// Per title, every build: the start of the simulation tick in progress and the smoothed period (shot_lag).
struct TitleTickClock
{
    std::atomic<uint64_t> startUs{0};
    std::atomic<uint32_t> periodUs{0};
};
TitleTickClock g_titleTicks[kTitleSlots];

// A published intent's point and camera sample: on foot P from its camera sample, seated the solve's point from the
// solve's eye (stamped with the newest render at the solve).
SightPointSample SampleFromIntent(const SightIntent& intent, bool seated)
{
    const shot_lag::IntentPoint& p = intent.p;
    SightPointSample sample{};
    memcpy(sample.point, seated ? intent.solvePoint : p.point, sizeof(sample.point));
    memcpy(sample.dir, p.dir, sizeof(sample.dir));
    sample.camera.valid = seated ? p.solveEyeStampUs != 0 : p.pointEyeValid;
    memcpy(sample.camera.eye, seated ? p.eye : p.pointEye, sizeof(sample.camera.eye));
    sample.camera.stampUs = seated ? p.solveEyeStampUs : p.pointEyeStampUs;
    sample.camera.velValid = p.eyeVelValid;
    memcpy(sample.camera.vel, p.eyeVel, sizeof(sample.camera.vel));
    return sample;
}
} // namespace

void Sight_NoteTitleTick(GameTitle title)
{
    auto& c = g_titleTicks[Slot(title)];
    const uint64_t now = Sight_NowUs();
    const uint64_t start = c.startUs.load(std::memory_order_relaxed);
    if (start && now > start && now - start <= 2000)
        return;   // another unit of the same tick
    c.startUs.store(now, std::memory_order_relaxed);
    if (start && now > start)
        c.periodUs.store(shot_lag::SmoothTickPeriod(c.periodUs.load(std::memory_order_relaxed), now - start),
                         std::memory_order_relaxed);
    Sight_NoteSimTick(title);   // dev: the SHOT LAG line's measured tick
}

uint32_t Sight_TickPeriodUs(GameTitle title, uint32_t fallbackUs)
{
    const uint32_t period = g_titleTicks[Slot(title)].periodUs.load(std::memory_order_relaxed);
    return period ? period : fallbackUs;
}

shot_lag::Transport Sight_TransportIntent(GameTitle title, const SightIntent& intent, bool seated,
                                          const float* siteEye, bool askEvaluator, float out[3], float shift[3],
                                          shot_lag::FireEye* fire, float* fireAgeMs, float* pointAgeMs)
{
    return Sight_TransportSample(title, SampleFromIntent(intent, seated), seated, siteEye, askEvaluator, out, shift,
                                 fire, fireAgeMs, pointAgeMs);
}

shot_lag::Transport Sight_TransportSample(GameTitle title, const SightPointSample& sample, bool seated,
                                          const float* siteEye, bool askEvaluator, float out[3], float shift[3],
                                          shot_lag::FireEye* fire, float* fireAgeMs, float* pointAgeMs)
{
    shot_lag::TransportInput in{};
    memcpy(in.point, sample.point, sizeof(in.point));
    in.pointEyeValid = sample.camera.valid;
    memcpy(in.pointEye, sample.camera.eye, sizeof(in.pointEye));
    in.pointEyeStampUs = sample.camera.stampUs;
    // A measured-only sample (Reach) never extrapolates: without a usable fire eye, P stays (Transport::NoFireEye).
    in.velValid = sample.camera.velValid && !sample.measuredOnly;
    memcpy(in.vel, sample.camera.vel, sizeof(in.vel));
    in.nowUs = Sight_NowUs();
    in.maxAgeUs = kSightTransportMaxAgeUs;
    // E_fire: measured only on foot, and only from a title that publishes its fire-tick eye.
    shot_lag::FireEye verdict = seated ? shot_lag::FireEye::Seated : shot_lag::FireEye::NoSource;
    float ageMs = -1.0f;
    if (!seated)
        if (const SightFireEyeFn source = g_fireEyeSources[Slot(title)].load(std::memory_order_acquire))
            verdict = source(siteEye, askEvaluator, sample, in.fireEye, ageMs);
    in.fireEyeValid = shot_lag::FireEyeUsable(verdict);
    if (fire)
        *fire = verdict;
    if (fireAgeMs)
        *fireAgeMs = in.fireEyeValid ? ageMs : -1.0f;
    if (pointAgeMs)
        *pointAgeMs = in.pointEyeStampUs && in.nowUs >= in.pointEyeStampUs
            ? float(in.nowUs - in.pointEyeStampUs) / 1000.0f : -1.0f;
    return shot_lag::TransportPoint(in, out, shift);
}

void Sight_NoteTransport(GameTitle title, SightTransportSite site, shot_lag::Transport how, shot_lag::FireEye fire,
                         const float* shift, const float* dir, const float* vel, float fireAgeMs, float pointAgeMs)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    const size_t s = static_cast<size_t>(site);
    if (s >= static_cast<size_t>(SightTransportSite::Count))
        return;
    const float none[3]{};
    auto& w = g_transports[Slot(title)][s];
    AcquireSRWLockExclusive(&w.lock);
    w.window.Add(how, fire, shift ? shift : none, dir ? dir : none, vel, fireAgeMs, pointAgeMs);
    ReleaseSRWLockExclusive(&w.lock);
#else
    (void)title; (void)site; (void)how; (void)fire; (void)shift; (void)dir; (void)vel; (void)fireAgeMs;
    (void)pointAgeMs;
#endif
}

namespace
{
// The fire thread's last shot point decision (Sight_LastShotPoint): the dev impact witness judges each round against
// the very P' its shot was aimed through.
thread_local SightLastShotPoint t_lastShotPoint{};
} // namespace

bool Sight_LastShotPoint(SightLastShotPoint& out)
{
    out = t_lastShotPoint;
    return out.valid;
}

const float* Sight_ShotPoint(GameTitle title, SightTransportSite site, const SightIntent& intent, bool seated,
                             const float* reference, float storage[3], const float* siteEye)
{
    // Every build (flat parity; the dev key shot_point_transport, default 1, is the A/B). Never in a shared
    // simulation: the shot stays the game's own there, and the transport's fire eye is not asked for
    // (Sight_FireEyeAsked).
    if (!reference || !storage || !Sight_TransportWanted(title, seated) || !Sight_FireTimeChangesAllowed())
        return reference;
    return Sight_ShotPointSample(title, site, SampleFromIntent(intent, seated), seated, reference, storage, siteEye);
}

const float* Sight_ShotPointSample(GameTitle title, SightTransportSite site, const SightPointSample& sample,
                                   bool seated, const float* reference, float storage[3], const float* siteEye)
{
    if (!reference || !storage || !Sight_TransportWanted(title, seated) || !Sight_FireTimeChangesAllowed())
        return reference;
    float shift[3]{};
    shot_lag::FireEye fire = shot_lag::FireEye::Count;
    float fireAgeMs = -1.0f, pointAgeMs = -1.0f;
    const shot_lag::Transport how =
        Sight_TransportSample(title, sample, seated, siteEye, site == SightTransportSite::Muzzle, storage, shift,
                              &fire, &fireAgeMs, &pointAgeMs);
    Sight_NoteTransport(title, site, how, fire, shift, sample.dir, sample.camera.velValid ? sample.camera.vel : nullptr,
                        fireAgeMs, pointAgeMs);
    const bool applied = shot_lag::TransportApplied(how);
    SightLastShotPoint& last = t_lastShotPoint;
    last = SightLastShotPoint{};
    last.valid = applied;
    last.site = site;
    last.how = how;
    last.fire = fire;
    last.stampUs = Sight_NowUs();
    if (applied)
    {
        memcpy(last.point, storage, sizeof(last.point));
        memcpy(last.shift, shift, sizeof(last.shift));
    }
    return applied ? storage : reference;
}

namespace
{
// Cold worker (Sight_Report): one line per title and site that saw a shot in the window. The muzzle site counts
// "used" for a shot that took the intent point (the muzzle's own rails then decide, as before).
void ReportReaims(size_t slot)
{
    for (size_t s = 0; s < static_cast<size_t>(SightShotSite::Count); ++s)
    {
        auto& stats = g_reaim[slot][s];
        const uint64_t applied = stats.applied.exchange(0, std::memory_order_relaxed);
        const uint64_t through = stats.through.exchange(0, std::memory_order_relaxed);
        const uint64_t used = stats.skipped[0].exchange(0, std::memory_order_relaxed);
        uint64_t refused = 0, skipped = 0;
        char reasons[256]{};
        size_t length = 0;
        const auto add = [&](const char* name, uint64_t n) {
            if (!n || length + 1 >= sizeof(reasons))
                return;
            const int w = std::snprintf(reasons + length, sizeof(reasons) - length, "%s%s=%llu", length ? ", " : "",
                                        name, static_cast<unsigned long long>(n));
            if (w > 0)
                length += std::min(static_cast<size_t>(w), sizeof(reasons) - length - 1);
        };
        for (size_t r = 1; r < static_cast<size_t>(shot_lag::Reaim::Count); ++r)
        {
            const uint64_t n = stats.refused[r].exchange(0, std::memory_order_relaxed);
            refused += n;
            add(shot_lag::ReaimName(static_cast<shot_lag::Reaim>(r)), n);
        }
        for (size_t r = 1; r < static_cast<size_t>(shot_lag::IntentUse::Count); ++r)
        {
            const uint64_t n = stats.skipped[r].exchange(0, std::memory_order_relaxed);
            skipped += n;
            add(shot_lag::IntentUseName(static_cast<shot_lag::IntentUse>(r)), n);
        }
        const uint32_t bendSum = stats.bendSumMilliDeg.exchange(0, std::memory_order_relaxed);
        const uint32_t bendMax = stats.bendMaxMilliDeg.exchange(0, std::memory_order_relaxed);
        const uint64_t seatedApplied = stats.seatedApplied.exchange(0, std::memory_order_relaxed);
        const uint64_t seatedThrough = stats.seatedThrough.exchange(0, std::memory_order_relaxed);
        const uint32_t seatedBendSum = stats.seatedBendSumMilliDeg.exchange(0, std::memory_order_relaxed);
        const uint32_t seatedBendMax = stats.seatedBendMaxMilliDeg.exchange(0, std::memory_order_relaxed);
        if (!applied && !through && !refused && !skipped && !used)
            continue;
        // The seated part: with the seat's record chosen upstream (Halo 3) the game's own line should already be
        // through P, so a seated re-aim that still had to act is what a host firing from the record would miss by.
        char seatedPart[200]{};
        if (seatedApplied || seatedThrough)
            std::snprintf(seatedPart, sizeof(seatedPart),
                          "; seated: %llu still re-aimed at the fire -- the multiplayer residual, what a host firing "
                          "from the record alone misses by (mean %.2f deg, max %.2f) -- and %llu already through",
                          static_cast<unsigned long long>(seatedApplied),
                          seatedApplied ? double(seatedBendSum) / 1000.0 / double(seatedApplied) : 0.0,
                          double(seatedBendMax) / 1000.0, static_cast<unsigned long long>(seatedThrough));
        if (static_cast<SightShotSite>(s) == SightShotSite::Muzzle)
        {
            LOG("SHOT REAIM %s %s: %llu flew through the frame-coherent intent point, %llu through the ray's point "
                "as before%s%s%s", TitleName(static_cast<GameTitle>(slot)), SiteName(static_cast<SightShotSite>(s)),
                static_cast<unsigned long long>(used), static_cast<unsigned long long>(skipped),
                length ? " (" : "", length ? reasons : "", length ? ")" : "");
            continue;
        }
        LOG("SHOT REAIM %s %s: %llu re-aimed through the intent point from the query's own start (mean %.2f deg, "
            "max %.2f), %llu already through it (left alone), %llu left as the game built them%s%s%s%s",
            TitleName(static_cast<GameTitle>(slot)), SiteName(static_cast<SightShotSite>(s)),
            static_cast<unsigned long long>(applied),
            applied ? double(bendSum) / 1000.0 / double(applied) : 0.0, double(bendMax) / 1000.0,
            static_cast<unsigned long long>(through), static_cast<unsigned long long>(refused + skipped),
            length ? " (" : "", length ? reasons : "", length ? ")" : "", seatedPart);
    }
}
} // namespace

bool Sight_AimMuzzleShot(GameTitle title, uint32_t generation, const float before[3], const float position[3],
                         float direction[3], float up[3])
{
    SightRay ray{};
    if (!Sight_ReadRay(title, generation, kSightRayFreshMs, ray))
    {
        Sight_NoteMuzzleShotUnconverged(title);
        return false;
    }
    // THE INTENT POINT FIRST: the point built from one frame-coherent hand/camera pair, through which
    // the shot flies from this muzzle -- the ray's own head may be a camera a frame older than its hand. Without a
    // usable intent, the ray's point (Sight_AimMuzzleShotFromRay keeps every gate and count).
    SightIntent intent{};
    const shot_lag::IntentUse use = Sight_IntentForShot(title, kSightIntentShotMaxAgeMs, false, intent);
    Sight_NoteReaimSkipped(title, SightShotSite::Muzzle, use);
    // A seat flies through the point its record was bent through (a seat's line is only ever corrected, never
    // re-targeted: SightIntent::solvePoint); on foot through the frame-coherent point.
    // The dev key shot_point_transport moves that point with the eye since its camera sample (Sight_ShotPoint).
    if (use == shot_lag::IntentUse::Use && intent.p.generation == generation)
    {
        const bool seated = intent.p.seated;
        float transported[3]{};
        const float* point = Sight_ShotPoint(title, SightTransportSite::Muzzle, intent, seated,
                                             seated ? intent.solvePoint : intent.p.point, transported);
        return Sight_AimMuzzleShotThrough(title, point, intent.p.dir, before, position, direction, up);
    }
    return Sight_AimMuzzleShotFromRay(title, generation, ray.head, ray.dir, ray.eye, before, position, direction, up);
}

bool Sight_ScopeConvergedLive(GameTitle title)
{
    // A seat clamped at a natural stop: the hand's ray runs past the stop the gun is held at, so the lens keeps the
    // game's own (clamped) aim, as it does under the stick servo.
    return g_config.scope_converge && Sight_AimEnabled() && Sight_ConvergedLive(title) &&
           Sight_AimWrittenLive(title) && !Sight_SeatLimitClampedLive(title);
}

bool Sight_ScopeConvergedDirection(GameTitle title, float outDir[3])
{
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    SightRay ray{};
    if (!outDir || !generation || !Sight_ScopeConvergedLive(title) ||
        !Sight_ReadRay(title, generation, kSightRayFreshMs, ray))
        return false;
    const float length = std::sqrt(ray.dir[0] * ray.dir[0] + ray.dir[1] * ray.dir[1] + ray.dir[2] * ray.dir[2]);
    if (!std::isfinite(length) || length < 1.0e-3f)
        return false;
    for (int axis = 0; axis < 3; ++axis)
        outDir[axis] = ray.dir[axis] / length;
    return true;
}

bool Game_SightReticleRange(float& outRangeM, bool& outHit)
{
    outRangeM = 0.0f;
    outHit = false;
    const GameTitle title = TitleAdapter_GetActiveTitle();
    if (title == GameTitle::HaloReach || title == GameTitle::None || title == GameTitle::Unknown)
        return false;
    if (!Sight_ConvergedLive(title))
        return false;
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    SightTrace trace{};
    if (!Sight_ReadTrace(title, generation, kSightFreshMs, trace))
        return false;
    const float scale = g_converged[Slot(title)].unitsPerVrMetre.load(std::memory_order_relaxed);
    if (!std::isfinite(scale) || !(scale > 0.0f))
        return false;
    outHit = trace.hit;
    outRangeM = trace.hit ? trace.rangeWu / scale : 0.0f;
    return true;
}

#if HALOMCCVR_DEV_DIAGNOSTICS
namespace
{
// The shot witness's per-title sums since the last report (mm), and the last shot's origin offsets.
struct ShotWitness
{
    // One lock keeps a shot's sums and its count in the same report window: NoteShot adds both under it and
    // ReportShots takes both under it. Without it a shot straddling a report split across two windows, and a
    // window whose count was 0 dropped the sums and logged the next window's shot as a perfect +0 mm.
    SRWLOCK lock = SRWLOCK_INIT;
    std::atomic<uint32_t> count{0}, unmeasured{0};
    std::atomic<float> surfaceUp{0.0f}, surfaceRight{0.0f}, pointUp{0.0f}, pointRight{0.0f};
    std::atomic<float> surfaceM{0.0f}, pointM{0.0f};
    std::atomic<float> fromEye[3]{}, fromHead[3]{};
};
ShotWitness g_shots[kTitleSlots];
constexpr float kMmPerWu = 3048.0f;

// SHOT LAG (sight_layer.h): one window per title and path. The firing path takes the lock for one Add (plain sums,
// no allocation); the cold worker takes it to swap the window out.
struct ShotLagWitness
{
    SRWLOCK lock = SRWLOCK_INIT;
    shot_lag::ShotLagWindow window{};
    uint32_t predicted = 0, unpredicted = 0;
};
ShotLagWitness g_shotLag[kTitleSlots][static_cast<size_t>(SightShotPath::Count)];
// The simulation's tick period per title, measured by a per-tick hook (Sight_NoteSimTick).
struct SimTickClock
{
    std::atomic<uint64_t> lastUs{0}, sumUs{0};
    std::atomic<uint32_t> ticks{0};
};
SimTickClock g_simTicks[kTitleSlots];

const char* ShotPathName(SightShotPath path)
{
    switch (path)
    {
    case SightShotPath::Native: return "native";
    case SightShotPath::CrosshairAssist: return "crosshair-assist";
    case SightShotPath::Muzzle: return "muzzle";
    case SightShotPath::UnitAdjust: return "unit-adjust";
    default: return "?";
    }
}

void AddRelaxed(std::atomic<float>& total, float value)
{
    float current = total.load(std::memory_order_relaxed);
    while (!total.compare_exchange_weak(current, current + value, std::memory_order_relaxed))
    {
    }
}

struct EngineRayEntry
{
    std::atomic<uint32_t> rva{0}, profile{0};
    std::atomic<uint64_t> flags{0};
    std::atomic<uint32_t> count{0}, aligned{0};
    std::atomic<float> lengthM{0.0f}, startMm{0.0f}, angleDeg{0.0f}, missMm{0.0f}, rangeM{0.0f};
};
constexpr size_t kEngineRaySlots = 48;
EngineRayEntry g_engineRays[kTitleSlots][kEngineRaySlots];
std::atomic<uint32_t> g_engineRayTitles{0};
constexpr float kMetresPerWu = 3.048f;
} // namespace
#endif

void Sight_NoteEngineRay(GameTitle title, uint32_t generation, uint32_t callerRva, uint32_t profile,
                         uint64_t flags, const float start[3], const float end[3])
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (!callerRva || !start || !end)
        return;
    const size_t slot = Slot(title);
    g_engineRayTitles.fetch_or(1u << slot, std::memory_order_relaxed);
    EngineRayEntry* entry = nullptr;
    for (auto& candidate : g_engineRays[slot])
    {
        uint32_t expected = 0;
        if (candidate.rva.load(std::memory_order_acquire) == callerRva ||
            candidate.rva.compare_exchange_strong(expected, callerRva, std::memory_order_acq_rel) ||
            expected == callerRva)
        {
            entry = &candidate;
            break;
        }
    }
    if (!entry)
        return;
    entry->count.fetch_add(1, std::memory_order_relaxed);
    entry->profile.store(profile, std::memory_order_relaxed);
    entry->flags.store(flags, std::memory_order_relaxed);
    const float v[3]{end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!std::isfinite(length) || length < 1.0e-6f)
        return;
    entry->lengthM.store(length * kMetresPerWu, std::memory_order_relaxed);
    SightRay ray{};
    SightTrace trace{};
    if (!Sight_ReadRay(title, generation, kSightRayFreshMs, ray) ||
        !Sight_ReadTrace(title, generation, kSightFreshMs, trace))
        return;
    const float aimRange = trace.AimRangeWu();
    const float p[3]{ray.head[0] + ray.dir[0] * aimRange, ray.head[1] + ray.dir[1] * aimRange,
                     ray.head[2] + ray.dir[2] * aimRange};
    const float a[3]{p[0] - ray.eye[0], p[1] - ray.eye[1], p[2] - ray.eye[2]};
    const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!(al > 1.0e-6f))
        return;
    float dot = (a[0] * v[0] + a[1] * v[1] + a[2] * v[2]) / (al * length);
    dot = dot > 1.0f ? 1.0f : (dot < -1.0f ? -1.0f : dot);
    const float angle = std::acos(dot) * 57.29578f;
    const float s[3]{start[0] - ray.eye[0], start[1] - ray.eye[1], start[2] - ray.eye[2]};
    const float startMm = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]) * kMetresPerWu * 1000.0f;
    if (startMm >= 300.0f || angle >= 2.0f)
        return;
    const float d[3]{v[0] / length, v[1] / length, v[2] / length};
    const float w[3]{p[0] - start[0], p[1] - start[1], p[2] - start[2]};
    const float c[3]{w[1] * d[2] - w[2] * d[1], w[2] * d[0] - w[0] * d[2], w[0] * d[1] - w[1] * d[0]};
    entry->aligned.fetch_add(1, std::memory_order_relaxed);
    entry->startMm.store(startMm, std::memory_order_relaxed);
    entry->angleDeg.store(angle, std::memory_order_relaxed);
    entry->missMm.store(std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) * kMetresPerWu * 1000.0f,
                        std::memory_order_relaxed);
    entry->rangeM.store(std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) * kMetresPerWu,
                        std::memory_order_relaxed);
#else
    (void)title; (void)generation; (void)callerRva; (void)profile; (void)flags; (void)start; (void)end;
#endif
}

void Sight_NoteShot(GameTitle title, uint32_t generation, const float origin[3], const float direction[3])
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    auto& w = g_shots[Slot(title)];
    SightRay ray{};
    SightTrace trace{};
    const float length = origin && direction
        ? std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2])
        : 0.0f;
    if (!std::isfinite(length) || length < 1.0e-6f || !Sight_ReadRay(title, generation, kSightRayFreshMs, ray) ||
        !Sight_ReadTrace(title, generation, kSightFreshMs, trace))
    {
        w.unmeasured.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(origin[axis]))
        {
            w.unmeasured.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    // The intent ray's own frame: forward along it, right level (Blam is Z-up), up perpendicular to both.
    const float f[3]{ray.dir[0], ray.dir[1], ray.dir[2]};
    float right[3]{f[1], -f[0], 0.0f};
    const float rightLength = std::sqrt(right[0] * right[0] + right[1] * right[1]);
    if (!std::isfinite(rightLength) || rightLength < 1.0e-3f)   // straight up or down: no level right
    {
        w.unmeasured.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    right[0] /= rightLength;
    right[1] /= rightLength;
    const float up[3]{right[1] * f[2] - right[2] * f[1], right[2] * f[0] - right[0] * f[2],
                      right[0] * f[1] - right[1] * f[0]};
    const float u[3]{direction[0] / length, direction[1] / length, direction[2] / length};
    const auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    // Where the shot's line passes the point `range` along the intent ray, measured from that point.
    const auto passes = [&](float range, float& outUp, float& outRight) {
        const float point[3]{ray.head[0] + f[0] * range, ray.head[1] + f[1] * range, ray.head[2] + f[2] * range};
        const float toPoint[3]{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
        const float t = dot(toPoint, u);
        const float miss[3]{origin[0] + u[0] * t - point[0], origin[1] + u[1] * t - point[1],
                            origin[2] + u[2] * t - point[2]};
        outUp = dot(miss, up) * kMmPerWu;
        outRight = dot(miss, right) * kMmPerWu;
    };
    float surfaceUp = 0.0f, surfaceRight = 0.0f, pointUp = 0.0f, pointRight = 0.0f;
    passes(trace.rangeWu, surfaceUp, surfaceRight);
    passes(trace.AimRangeWu(), pointUp, pointRight);
    if (!std::isfinite(surfaceUp) || !std::isfinite(surfaceRight) || !std::isfinite(pointUp) ||
        !std::isfinite(pointRight))
    {
        w.unmeasured.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const float fromEye[3]{origin[0] - ray.eye[0], origin[1] - ray.eye[1], origin[2] - ray.eye[2]};
    const float fromHead[3]{origin[0] - ray.head[0], origin[1] - ray.head[1], origin[2] - ray.head[2]};
    AcquireSRWLockExclusive(&w.lock);
    AddRelaxed(w.surfaceUp, surfaceUp);
    AddRelaxed(w.surfaceRight, surfaceRight);
    AddRelaxed(w.pointUp, pointUp);
    AddRelaxed(w.pointRight, pointRight);
    w.surfaceM.store(trace.rangeWu * kSightCmPerWorldUnit * 0.01f, std::memory_order_relaxed);
    w.pointM.store(trace.AimRangeWu() * kSightCmPerWorldUnit * 0.01f, std::memory_order_relaxed);
    w.fromEye[0].store(dot(fromEye, right) * kMmPerWu, std::memory_order_relaxed);
    w.fromEye[1].store(dot(fromEye, up) * kMmPerWu, std::memory_order_relaxed);
    w.fromEye[2].store(dot(fromEye, f) * kMmPerWu, std::memory_order_relaxed);
    w.fromHead[0].store(dot(fromHead, right) * kMmPerWu, std::memory_order_relaxed);
    w.fromHead[1].store(dot(fromHead, up) * kMmPerWu, std::memory_order_relaxed);
    w.fromHead[2].store(dot(fromHead, f) * kMmPerWu, std::memory_order_relaxed);
    w.count.fetch_add(1, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&w.lock);
#else
    (void)title; (void)generation; (void)origin; (void)direction;
#endif
}

void Sight_NoteShotLag(GameTitle title, SightShotPath path, const float origin[3], const float direction[3],
                       const float reference[3], const float* solveEye, const float* eyeVel, int predicted,
                       bool seated, int32_t seat)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    const size_t p = static_cast<size_t>(path);
    if (p >= static_cast<size_t>(SightShotPath::Count))
        return;
    shot_lag::ShotLagSample sample{};
    if (origin && direction && reference)
        sample = shot_lag::MeasureShotLag(origin, direction, reference, solveEye, eyeVel);
    auto& w = g_shotLag[Slot(title)][p];
    AcquireSRWLockExclusive(&w.lock);
    w.window.Add(sample, seated, seat);
    if (predicted > 0)
        ++w.predicted;
    else if (predicted == 0)
        ++w.unpredicted;
    ReleaseSRWLockExclusive(&w.lock);
#else
    (void)title; (void)path; (void)origin; (void)direction; (void)reference; (void)solveEye; (void)eyeVel;
    (void)predicted; (void)seated; (void)seat;
#endif
}

void Sight_NoteShotLagFromIntent(GameTitle title, SightShotPath path, const float origin[3], const float direction[3],
                                 int predicted, bool seated, int32_t seat)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    SightIntent intent{};
    const uint64_t nowUs = Sight_NowUs();
    // The point the player meant: the published intent (any converged state; the witness judges every shot), else
    // the intent ray at the range the aim converged on.
    if (Sight_ReadIntent(title, intent) && generation && intent.p.generation == generation && intent.p.stampUs &&
        nowUs >= intent.p.stampUs && nowUs - intent.p.stampUs <= 250000ull)
    {
        Sight_NoteShotLag(title, path, origin, direction, intent.p.point, intent.p.eye,
                          intent.p.eyeVelValid ? intent.p.eyeVel : nullptr, predicted, seated, seat);
        return;
    }
    SightRay ray{};
    SightTrace trace{};
    if (Sight_ReadRay(title, generation, kSightRayFreshMs, ray) &&
        Sight_ReadTrace(title, generation, kSightFreshMs, trace) && trace.AimRangeWu() > 0.0f)
    {
        const float range = trace.AimRangeWu();
        const float point[3]{ray.head[0] + ray.dir[0] * range, ray.head[1] + ray.dir[1] * range,
                             ray.head[2] + ray.dir[2] * range};
        Sight_NoteShotLag(title, path, origin, direction, point, ray.eye, ray.eyeVelValid ? ray.eyeVel : nullptr,
                          predicted, seated, seat);
        return;
    }
    Sight_NoteShotLag(title, path, nullptr, nullptr, nullptr, nullptr, nullptr, predicted, seated, seat);
#else
    (void)title; (void)path; (void)origin; (void)direction; (void)predicted; (void)seated; (void)seat;
#endif
}

#if HALOMCCVR_DEV_DIAGNOSTICS
namespace
{
std::atomic<uint32_t> g_intentPairing[kTitleSlots][4]{};
// The witness's own eye tracks, for titles that publish no ray (Halo 2, CE): fed from their firing sites.
shot_lag::EyeTrack g_witnessTracks[kTitleSlots];
SRWLOCK g_witnessTrackLock = SRWLOCK_INIT;
}
#endif

void Sight_NoteShotLagFromRay(GameTitle title, SightShotPath path, const float origin[3], const float direction[3],
                              const float head[3], const float rayDir[3], const float eye[3], bool seated)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    SightTrace trace{};
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    if (!head || !rayDir || !eye || !Sight_ReadTrace(title, generation, kSightFreshMs, trace) ||
        !(trace.AimRangeWu() > 0.0f))
    {
        Sight_NoteShotLag(title, path, nullptr, nullptr, nullptr, nullptr, nullptr, -1, seated, -1);
        return;
    }
    const float length = std::sqrt(rayDir[0] * rayDir[0] + rayDir[1] * rayDir[1] + rayDir[2] * rayDir[2]);
    if (!std::isfinite(length) || length < 1.0e-6f)
    {
        Sight_NoteShotLag(title, path, nullptr, nullptr, nullptr, nullptr, nullptr, -1, seated, -1);
        return;
    }
    const float range = trace.AimRangeWu();
    const float point[3]{head[0] + rayDir[0] / length * range, head[1] + rayDir[1] / length * range,
                         head[2] + rayDir[2] / length * range};
    float velocity[3]{};
    AcquireSRWLockExclusive(&g_witnessTrackLock);
    auto& track = g_witnessTracks[Slot(title)];
    track.Push(Sight_NowUs(), eye);
    const bool known = track.Velocity(120000, 30000, velocity);
    ReleaseSRWLockExclusive(&g_witnessTrackLock);
    Sight_NoteShotLag(title, path, origin, direction, point, eye, known ? velocity : nullptr, -1, seated, -1);
#else
    (void)title; (void)path; (void)origin; (void)direction; (void)head; (void)rayDir; (void)eye; (void)seated;
#endif
}

void Sight_NoteIntentPairing(GameTitle title, int kind)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (kind >= 0 && kind < 4)
        g_intentPairing[Slot(title)][kind].fetch_add(1, std::memory_order_relaxed);
#else
    (void)title; (void)kind;
#endif
}

void Sight_NoteSimTick(GameTitle title)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    auto& c = g_simTicks[Slot(title)];
    const uint64_t now = Sight_NowUs();
    const uint64_t last = c.lastUs.exchange(now, std::memory_order_relaxed);
    // Several units per tick come through the per-unit hooks: only a gap of over 2 ms is a new tick, and one of
    // over 250 ms is a stall, not a period.
    if (last && now > last && now - last > 2000 && now - last < 250000)
    {
        c.sumUs.fetch_add(now - last, std::memory_order_relaxed);
        c.ticks.fetch_add(1, std::memory_order_relaxed);
    }
    else if (last && now > last && now - last <= 2000)
        c.lastUs.store(last, std::memory_order_relaxed);   // the same tick: keep its start
#else
    (void)title;
#endif
}

void Sight_ReportShots()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    static uint64_t s_last = 0;
    const uint64_t now = Sight_NowMs();
    if (now - s_last < 2000)
        return;
    s_last = now;
    for (size_t slot = 0; slot < kTitleSlots; ++slot)
    {
        auto& w = g_shots[slot];
        // Take the window whole (see ShotWitness::lock), last-shot fields included.
        AcquireSRWLockExclusive(&w.lock);
        const uint32_t count = w.count.exchange(0, std::memory_order_relaxed);
        const uint32_t unmeasured = w.unmeasured.exchange(0, std::memory_order_relaxed);
        const float surfaceUp = w.surfaceUp.exchange(0.0f, std::memory_order_relaxed);
        const float surfaceRight = w.surfaceRight.exchange(0.0f, std::memory_order_relaxed);
        const float pointUp = w.pointUp.exchange(0.0f, std::memory_order_relaxed);
        const float pointRight = w.pointRight.exchange(0.0f, std::memory_order_relaxed);
        const float surfaceM = w.surfaceM.load(std::memory_order_relaxed);
        const float pointM = w.pointM.load(std::memory_order_relaxed);
        float fromEye[3], fromHead[3];
        for (int axis = 0; axis < 3; ++axis)
        {
            fromEye[axis] = w.fromEye[axis].load(std::memory_order_relaxed);
            fromHead[axis] = w.fromHead[axis].load(std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&w.lock);
        if (!count && !unmeasured)
            continue;
        const auto title = static_cast<GameTitle>(slot);
        if (!count)
        {
            LOG("SHOT %s: %u shot(s), none measurable (no fresh intent ray or trace, or aimed straight up/down)",
                TitleName(title), unmeasured);
            continue;
        }
        const float n = static_cast<float>(count);
        LOG("SHOT %s: %u shot(s) (+%u unmeasured) pass %+.0f mm up / %+.0f mm right of the surface under the "
            "reticle (%.2f m) and %+.0f / %+.0f mm of the converged point (%.2f m), on average; the last left "
            "%+.0f/%+.0f/%+.0f mm (right/up/along) from the engine eye and %+.0f/%+.0f/%+.0f mm from the "
            "rendered eye", TitleName(title), count, unmeasured, double(surfaceUp / n), double(surfaceRight / n),
            double(surfaceM), double(pointUp / n), double(pointRight / n), double(pointM), double(fromEye[0]),
            double(fromEye[1]), double(fromEye[2]), double(fromHead[0]), double(fromHead[1]),
            double(fromHead[2]));
    }
    // SHOT LAG, per title and path.
    for (size_t slot = 0; slot < kTitleSlots; ++slot)
    {
        uint32_t pairing[4]{};
        for (int k = 0; k < 4; ++k)
            pairing[k] = g_intentPairing[slot][k].exchange(0, std::memory_order_relaxed);
        if (pairing[0] || pairing[1] || pairing[2] || pairing[3])
            LOG("SHOT LAG %s intent point (dev): %u from this poll's hand and the render of its own frame, %u from an "
                "earlier hand and the render of its frame (this poll's frame not rendered yet), %u from the solve's "
                "own ray (no kept hand had a rendered frame), %u without camera frames",
                TitleName(static_cast<GameTitle>(slot)), pairing[0], pairing[1], pairing[2], pairing[3]);
        const uint32_t ticks = g_simTicks[slot].ticks.exchange(0, std::memory_order_relaxed);
        const uint64_t tickSum = g_simTicks[slot].sumUs.exchange(0, std::memory_order_relaxed);
        for (size_t p = 0; p < static_cast<size_t>(SightShotPath::Count); ++p)
        {
            auto& w = g_shotLag[slot][p];
            AcquireSRWLockExclusive(&w.lock);
            const shot_lag::ShotLagWindow window = w.window;
            const uint32_t predicted = w.predicted, unpredicted = w.unpredicted;
            w.window = shot_lag::ShotLagWindow{};
            w.predicted = w.unpredicted = 0;
            ReleaseSRWLockExclusive(&w.lock);
            if (!window.shots && !window.unmeasured)
                continue;
            char label[160]{};
            char mode[64]{};
            if (predicted || unpredicted)
                std::snprintf(mode, sizeof(mode), "fire predicted %u/%u", predicted, predicted + unpredicted);
            else
                std::snprintf(mode, sizeof(mode), "fire predicted n/a");
            char tick[48]{};
            if (ticks)
                std::snprintf(tick, sizeof(tick), "; sim tick %.1f ms", double(tickSum) / double(ticks) / 1000.0);
            std::snprintf(label, sizeof(label), "%s [%s; %s%s]", TitleName(static_cast<GameTitle>(slot)),
                          ShotPathName(static_cast<SightShotPath>(p)), mode, tick);
            char line[1024]{};
            if (window.Format(label, line, sizeof(line)))
                LOG("%s", line);
        }
        // The shot point moved with the eye (shot_point_transport), per consumer: what was used, and the shift.
        for (size_t s = 0; s < static_cast<size_t>(SightTransportSite::Count); ++s)
        {
            auto& w = g_transports[slot][s];
            AcquireSRWLockExclusive(&w.lock);
            const shot_lag::TransportWindow window = w.window;
            w.window = shot_lag::TransportWindow{};
            ReleaseSRWLockExclusive(&w.lock);
            char label[96]{};
            std::snprintf(label, sizeof(label), "SHOT LAG %s transport [%s] (dev)",
                          TitleName(static_cast<GameTitle>(slot)), TransportSiteName(static_cast<SightTransportSite>(s)));
            char line[768]{};
            if (window.Format(label, line, sizeof(line)))
                LOG("%s", line);
        }
    }
    // The keys themselves, whenever they change (and once at the first report), so a log names its A/B arm.
    static int s_key = -1, s_seatedKey = -1;
    if (g_config.shot_point_transport != s_key || g_config.shot_point_transport_seated != s_seatedKey)
    {
        s_key = g_config.shot_point_transport;
        s_seatedKey = g_config.shot_point_transport_seated;
        LOG("SHOT LAG transport (dev): shot_point_transport=%d shot_point_transport_seated=%d -- %s", s_key,
            s_seatedKey, s_key ? (s_seatedKey ? "shots aim through P moved with the eye, on foot and seated (Reach: on "
                                                "foot only, and only with a measured fire eye)"
                                              : "on-foot shots aim through P moved with the eye (Reach: only with a "
                                                "measured fire eye); seats through P")
                               : "every shot through P as published (the SHOT IMPACT vs P' columns say where a moved "
                                 "point would have been)");
    }
#endif
}

void Sight_ReportEngineRays()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    static uint64_t s_last = 0;
    const uint64_t now = Sight_NowMs();
    if (now - s_last < 5000)
        return;
    s_last = now;
    const uint32_t titles = g_engineRayTitles.load(std::memory_order_relaxed);
    for (size_t slot = 0; slot < kTitleSlots; ++slot)
    {
        if (!(titles & (1u << slot)))
            continue;
        const auto title = static_cast<GameTitle>(slot);
        uint32_t callers = 0, queries = 0;
        for (auto& entry : g_engineRays[slot])
        {
            const uint32_t rva = entry.rva.load(std::memory_order_acquire);
            if (!rva)
                continue;
            const uint32_t count = entry.count.exchange(0, std::memory_order_relaxed);
            const uint32_t aligned = entry.aligned.exchange(0, std::memory_order_relaxed);
            if (count)
            {
                ++callers;
                queries += count;
            }
            if (aligned)
                LOG("SIGHT RAYS %s +0x%X profile 0x%X flags 0x%llX: %u queries, %u ALIGNED (start %.0f mm from the "
                    "eye, %.2f deg from eye->point; the traced point %.0f mm off this line at %.2f m), %.1f m long",
                    TitleName(title), rva, entry.profile.load(), (unsigned long long)entry.flags.load(), count,
                    aligned, double(entry.startMm.load()), double(entry.angleDeg.load()),
                    double(entry.missMm.load()), double(entry.rangeM.load()), double(entry.lengthM.load()));
        }
        LOG("SIGHT RAYS %s: %u callers, %u queries in 5 s", TitleName(title), callers, queries);
    }
#endif
}

// ---- what the intent ray can hit (sight_hit_class_logic.h) ---------------------------------------------------
namespace
{
#if HALOMCCVR_DEV_DIAGNOSTICS
constexpr size_t kHitClasses = static_cast<size_t>(sight_hit::HitClass::Count);
// One writer per title (its trace thread); the cold worker drains it. A torn near/far only blurs one dev line.
struct HitClassWindow
{
    std::atomic<uint32_t> count[kHitClasses]{}, lookPast[kHitClasses]{}, seated{0};
    std::atomic<float> nearWu[kHitClasses]{}, farWu[kHitClasses]{};
    std::atomic<int32_t> lastType[kHitClasses]{};
};
HitClassWindow g_hitClasses[kTitleSlots];

void ReportHitClasses(size_t slot)
{
    auto& w = g_hitClasses[slot];
    uint32_t count[kHitClasses]{}, past[kHitClasses]{}, traces = 0, looked = 0;
    for (size_t c = 0; c < kHitClasses; ++c)
    {
        count[c] = w.count[c].exchange(0, std::memory_order_relaxed);
        past[c] = w.lookPast[c].exchange(0, std::memory_order_relaxed);
        traces += count[c];
        looked += past[c];
    }
    const uint32_t seated = w.seated.exchange(0, std::memory_order_relaxed);
    if (!traces && !looked)
        return;
    const double mPerWu = double(kSightCmPerWorldUnit) * 0.01;
    char text[4][72]{};
    static constexpr sight_hit::HitClass kRanged[4]{sight_hit::HitClass::Structure, sight_hit::HitClass::Biped,
                                                    sight_hit::HitClass::Vehicle, sight_hit::HitClass::OtherObject};
    for (size_t i = 0; i < 4; ++i)
    {
        const size_t c = static_cast<size_t>(kRanged[i]);
        if (!count[c])
            std::snprintf(text[i], sizeof(text[i]), "%s 0", sight_hit::HitClassName(kRanged[i]));
        else if (kRanged[i] == sight_hit::HitClass::OtherObject)
            std::snprintf(text[i], sizeof(text[i]), "%s %u [%.2f-%.2f m, last type %d]",
                          sight_hit::HitClassName(kRanged[i]), count[c],
                          double(w.nearWu[c].load(std::memory_order_relaxed)) * mPerWu,
                          double(w.farWu[c].load(std::memory_order_relaxed)) * mPerWu,
                          static_cast<int>(w.lastType[c].load(std::memory_order_relaxed)));
        else
            std::snprintf(text[i], sizeof(text[i]), "%s %u [%.2f-%.2f m]", sight_hit::HitClassName(kRanged[i]),
                          count[c], double(w.nearWu[c].load(std::memory_order_relaxed)) * mPerWu,
                          double(w.farWu[c].load(std::memory_order_relaxed)) * mPerWu);
    }
    LOG("SIGHT HITS %s (dev): %u traces (%u seated): %s, %s, %s, %s, unknown-type object %u, miss %u [= the far "
        "end]; looked past our own vehicle: %u (biped %u, vehicle %u, other %u, unknown %u)",
        TitleName(static_cast<GameTitle>(slot)), traces, seated, text[0], text[1], text[2], text[3],
        count[static_cast<size_t>(sight_hit::HitClass::UnknownObject)],
        count[static_cast<size_t>(sight_hit::HitClass::Miss)], looked,
        past[static_cast<size_t>(sight_hit::HitClass::Biped)], past[static_cast<size_t>(sight_hit::HitClass::Vehicle)],
        past[static_cast<size_t>(sight_hit::HitClass::OtherObject)],
        past[static_cast<size_t>(sight_hit::HitClass::UnknownObject)]);
}
#endif
} // namespace

void Sight_AnnounceTraceQuery(GameTitle title, sight_hit::Engine engine, uint64_t flags, const char* query,
                              const char* source, const char* seatedNote)
{
    const sight_hit::FlagClasses c = sight_hit::DecodeTraceFlags(engine, flags);
    const sight_hit::FlagLayout layout = sight_hit::Layout(engine);
    char types[96]{};
    if (!c.objects)
        std::snprintf(types, sizeof(types), "no objects at all");
    else if (c.allTypes)
        std::snprintf(types, sizeof(types), "every one of %u object types", static_cast<unsigned>(layout.typeCount));
    else
        std::snprintf(types, sizeof(types), "object type mask 0x%X of %u types", c.typeMask,
                      static_cast<unsigned>(layout.typeCount));
    char culls[176]{};
    if (!c.cullsDecoded)
        std::snprintf(culls, sizeof(culls), "; the engine's per-object culls were not decoded for this title");
    else if (c.objectCulls || c.bipedCulls || c.vehicleCulls)
        std::snprintf(culls, sizeof(culls), "; the engine's own culls in these flags: objects 0x%llX, bipeds 0x%llX, "
                      "vehicles 0x%llX (flag bits)", static_cast<unsigned long long>(c.objectCulls),
                      static_cast<unsigned long long>(c.bipedCulls), static_cast<unsigned long long>(c.vehicleCulls));
    LOG("SIGHT %s intent ray: %s, flags 0x%llX (%s): stops on structure %s, bipeds %s, vehicles %s, other objects %s "
        "(%s)%s; %s",
        TitleName(title), query ? query : "?", static_cast<unsigned long long>(flags), source ? source : "?",
        c.structure ? "yes" : "NO", c.bipeds ? "yes" : "NO", c.vehicles ? "yes" : "NO",
        c.otherObjects ? "yes" : "NO", types, culls, seatedNote ? seatedNote : "");
    if (!sight_hit::HitsUnitsAndVehicles(c))
        LOG("SIGHT %s WARNING: the intent ray cannot stop on %s%s%s-- the converged aim will read the surface BEHIND "
            "such a target and converge too far",
            TitleName(title), c.structure ? "" : "structure ", c.bipeds ? "" : "bipeds ",
            c.vehicles ? "" : "vehicles ");
}

void Sight_NoteTraceHit(GameTitle title, sight_hit::HitClass hit, int objectType, float rangeWu, bool seated)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    const size_t c = static_cast<size_t>(hit);
    if (c >= kHitClasses)
        return;
    auto& w = g_hitClasses[Slot(title)];
    const uint32_t prior = w.count[c].fetch_add(1, std::memory_order_relaxed);
    if (seated)
        w.seated.fetch_add(1, std::memory_order_relaxed);
    if (hit == sight_hit::HitClass::Miss)
        return;
    if (!prior || rangeWu < w.nearWu[c].load(std::memory_order_relaxed))
        w.nearWu[c].store(rangeWu, std::memory_order_relaxed);
    if (!prior || rangeWu > w.farWu[c].load(std::memory_order_relaxed))
        w.farWu[c].store(rangeWu, std::memory_order_relaxed);
    w.lastType[c].store(objectType, std::memory_order_relaxed);
#else
    (void)title; (void)hit; (void)objectType; (void)rangeWu; (void)seated;
#endif
}

void Sight_NoteLookPast(GameTitle title, sight_hit::HitClass part)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    const size_t c = static_cast<size_t>(part);
    if (c < kHitClasses)
        g_hitClasses[Slot(title)].lookPast[c].fetch_add(1, std::memory_order_relaxed);
#else
    (void)title; (void)part;
#endif
}

// ---- the seat convergence witness, Halo CE and Halo 2 (seat_converge_logic.h) -----------------------------------
namespace
{
constexpr size_t kSeatWhys = static_cast<size_t>(seat_converge::Why::Count);
constexpr size_t kSeatLines = static_cast<size_t>(SightSeatLine::Count);
struct SeatWitness
{
    // The seat the counters below belong to (the newest), and how often the seat changed in the window.
    std::atomic<uint32_t> seatKey{0}, roleBits{0}, seatChanges{0};
    std::atomic<uint32_t> polls{0};
    std::atomic<uint32_t> why[kSeatWhys]{};
    // The last converged update (Sight_SeatConvergedLive).
    std::atomic<uint64_t> convergedMs{0};
    std::atomic<uint32_t> convergedKey{0}, convergedGeneration{0};
};
SeatWitness g_seats[kTitleSlots];
#if HALOMCCVR_DEV_DIAGNOSTICS
struct SeatShotWitness
{
    SRWLOCK lock = SRWLOCK_INIT;
    seat_converge::MissWindow window[kSeatLines]{};
};
SeatShotWitness g_seatShots[kTitleSlots];
#endif

// Cold worker (Sight_Report): the seat line, and in dev builds the seated shot lines.
void ReportSeats(size_t slot)
{
    auto& w = g_seats[slot];
    const uint32_t polls = w.polls.exchange(0, std::memory_order_relaxed);
    uint32_t counts[kSeatWhys]{};
    for (size_t r = 0; r < kSeatWhys; ++r)
        counts[r] = w.why[r].exchange(0, std::memory_order_relaxed);
    const uint32_t changes = w.seatChanges.exchange(0, std::memory_order_relaxed);
    const auto title = static_cast<GameTitle>(slot);
    if (polls)
    {
        char reasons[320]{};
        size_t length = 0;
        for (size_t r = 1; r < kSeatWhys; ++r)
        {
            if (!counts[r] || length + 1 >= sizeof(reasons))
                continue;
            const int n = std::snprintf(reasons + length, sizeof(reasons) - length, "%s%s=%u", length ? ", " : "",
                                        seat_converge::WhyName(static_cast<seat_converge::Why>(r)), counts[r]);
            if (n > 0)
                length += std::min(static_cast<size_t>(n), sizeof(reasons) - length - 1);
        }
        const uint32_t bits = w.roleBits.load(std::memory_order_relaxed);
        seat_converge::Role role{};
        role.known = (bits & 1u) != 0;
        role.driver = (bits & 2u) != 0;
        role.gunner = (bits & 4u) != 0;
        char changed[64]{};
        if (changes > 1)
            std::snprintf(changed, sizeof(changed), " (%u seat changes in this window)", changes - 1);
        LOG("SIGHT %s seat 0x%08X (%s): %u aim updates, %u converged on the traced point%s%s%s",
            TitleName(title), w.seatKey.load(std::memory_order_relaxed), seat_converge::RoleName(role), polls,
            counts[0], length ? "; not converged (the seat kept its earlier aim): " : "; every update converged",
            length ? reasons : "", changed);
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    seat_converge::MissWindow lines[kSeatLines]{};
    auto& shots = g_seatShots[slot];
    AcquireSRWLockExclusive(&shots.lock);
    for (size_t l = 0; l < kSeatLines; ++l)
    {
        lines[l] = shots.window[l];
        shots.window[l] = seat_converge::MissWindow{};
    }
    ReleaseSRWLockExclusive(&shots.lock);
    const auto& barrel = lines[static_cast<size_t>(SightSeatLine::Barrel)];
    const auto& crosshair = lines[static_cast<size_t>(SightSeatLine::Crosshair)];
    if (barrel.n || crosshair.n)
    {
        constexpr double kMmPerWuD = 3048.0, kMPerWu = 3.048;
        const auto mean = [](double sum, uint32_t n) { return n ? sum / double(n) : 0.0; };
        LOG("SIGHT %s seat shots (dev): the traced point against the lines the ENGINE built, before the fire's "
            "re-aim -- barrel/unit line (the shot as the fire took it: the centre of the seat's deviation cone) %u: "
            "mean %.0f mm / %.2f deg (max %.0f mm / %.2f deg) at %.1f m; engine crosshair line (its own "
            "deterministic camera along the unit's aim: what an unmodded host traces along) %u: mean %.0f mm / "
            "%.2f deg (max %.0f mm / %.2f deg) at %.1f m",
            TitleName(title), barrel.n, mean(barrel.sumWu, barrel.n) * kMmPerWuD, mean(barrel.sumDeg, barrel.n),
            double(barrel.maxWu) * kMmPerWuD, double(barrel.maxDeg), mean(barrel.sumAlongWu, barrel.n) * kMPerWu,
            crosshair.n, mean(crosshair.sumWu, crosshair.n) * kMmPerWuD, mean(crosshair.sumDeg, crosshair.n),
            double(crosshair.maxWu) * kMmPerWuD, double(crosshair.maxDeg),
            mean(crosshair.sumAlongWu, crosshair.n) * kMPerWu);
    }
#endif
}
} // namespace

void Sight_NoteSeatPoll(GameTitle title, uint32_t seatKey, const seat_converge::Role& role, seat_converge::Why why)
{
    auto& w = g_seats[Slot(title)];
    if (w.seatKey.exchange(seatKey, std::memory_order_relaxed) != seatKey ||
        !w.seatChanges.load(std::memory_order_relaxed))
        w.seatChanges.fetch_add(1, std::memory_order_relaxed);
    w.roleBits.store((role.known ? 1u : 0u) | (role.driver ? 2u : 0u) | (role.gunner ? 4u : 0u),
                     std::memory_order_relaxed);
    w.polls.fetch_add(1, std::memory_order_relaxed);
    const size_t r = static_cast<size_t>(why);
    if (r < kSeatWhys)
        w.why[r].fetch_add(1, std::memory_order_relaxed);
    if (why == seat_converge::Why::Converged)
    {
        w.convergedKey.store(seatKey, std::memory_order_relaxed);
        w.convergedGeneration.store(TitleAdapter_GetGeneration(title), std::memory_order_relaxed);
        w.convergedMs.store(Sight_NowMs(), std::memory_order_release);
    }
}

bool Sight_SeatConvergedLive(GameTitle title, uint32_t seatKey, uint64_t maxAgeMs)
{
    const auto& w = g_seats[Slot(title)];
    const uint64_t at = w.convergedMs.load(std::memory_order_acquire);
    return Fresh(at, Sight_NowMs(), maxAgeMs) && w.convergedKey.load(std::memory_order_relaxed) == seatKey &&
           w.convergedGeneration.load(std::memory_order_relaxed) == TitleAdapter_GetGeneration(title);
}

void Sight_NoteSeatShot(GameTitle title, SightSeatLine line, const float origin[3], const float direction[3],
                        const float point[3])
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    const size_t l = static_cast<size_t>(line);
    seat_converge::LineMiss miss{};
    if (l >= kSeatLines || !origin || !direction || !point ||
        !seat_converge::MeasureLine(origin, direction, point, miss))
        return;
    auto& shots = g_seatShots[Slot(title)];
    AcquireSRWLockExclusive(&shots.lock);
    shots.window[l].add(miss);
    ReleaseSRWLockExclusive(&shots.lock);
#else
    (void)title; (void)line; (void)origin; (void)direction; (void)point;
#endif
}

namespace
{
// Cold worker (Sight_Report): what the written aim's lead did in the window, for a title whose on-foot writes were
// offered to it (the key on). The residual it leaves is on the SHOT REAIM line, or with co-op safe shots on the
// CO-OP SAFE SHOTS line's "on foot" part.
void ReportLead(size_t slot)
{
    using aim_lead::Outcome;
    using aim_lead::Restart;
    auto& lane = g_leads[slot];
    uint64_t outcomes[static_cast<size_t>(Outcome::Count)]{}, total = 0;
    for (size_t o = 0; o < static_cast<size_t>(Outcome::Count); ++o)
        total += (outcomes[o] = lane.outcomes[o].exchange(0, std::memory_order_relaxed));
    const uint64_t drawn = lane.drawn.exchange(0, std::memory_order_relaxed);
    const uint64_t jumps = lane.restarts[static_cast<size_t>(Restart::Jump)].exchange(0, std::memory_order_relaxed);
    const uint64_t gaps = lane.restarts[static_cast<size_t>(Restart::Gap)].exchange(0, std::memory_order_relaxed);
    lane.restarts[static_cast<size_t>(Restart::None)].store(0, std::memory_order_relaxed);
    lane.restarts[static_cast<size_t>(Restart::First)].store(0, std::memory_order_relaxed);
    const uint64_t capped = lane.capped.exchange(0, std::memory_order_relaxed);
    const uint64_t repeated = lane.repeated.exchange(0, std::memory_order_relaxed);
    const uint64_t sumMicroDeg = lane.leadSumMicroDeg.exchange(0, std::memory_order_relaxed);
    const uint32_t maxMilliDeg = lane.leadMaxMilliDeg.exchange(0, std::memory_order_relaxed);
    total += drawn;
    if (!total)
        return;
    const uint64_t led = outcomes[static_cast<size_t>(Outcome::Led)];
    LOG("AIM LEAD %s on foot: %llu of %llu written aims led by a mean of %.2f deg (max %.2f), %.1f ms ahead "
        "(aim_lead_ticks %.2f); written unchanged: %llu at rest, %llu starting, %llu stopping or turning back, %llu "
        "while a scope lens showed the game's aim; %llu led ones capped at %.1f deg; started afresh after %llu "
        "jumps and %llu gaps; %llu passes met a sample already seen",
        TitleName(static_cast<GameTitle>(slot)), (unsigned long long)led, (unsigned long long)total,
        led ? double(sumMicroDeg) / double(led) / 1.0e6 : 0.0, double(maxMilliDeg) / 1000.0,
        double(lane.leadUs.load(std::memory_order_relaxed)) / 1000.0,
        double(aim_lead::ClampTicks(g_config.aim_lead_ticks, 0.0f)),
        (unsigned long long)outcomes[static_cast<size_t>(Outcome::Resting)],
        (unsigned long long)outcomes[static_cast<size_t>(Outcome::Priming)],
        (unsigned long long)outcomes[static_cast<size_t>(Outcome::Stopping)], (unsigned long long)drawn,
        (unsigned long long)capped, double(aim_lead::Rails{}.maxLeadRad * aim_lead::kRadToDeg),
        (unsigned long long)jumps, (unsigned long long)gaps, (unsigned long long)repeated);
}
} // namespace

void Sight_Report()
{
    static uint64_t s_last = 0;
    const uint64_t now = Sight_NowMs();
    if (now - s_last < 2000)
        return;
    s_last = now;
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    const double usPerTick = freq.QuadPart ? 1.0e6 / double(freq.QuadPart) : 0.0;
    // The automatic switch: one line whenever what it decided, or why, changes.
    if (g_config.coop_safe_shots == 2)
    {
        auto& g = g_sessionGate;
        const int open = Sight_FireTimeChangesAllowed() ? 1 : 0;
        const uint8_t standing = g.standing.load(std::memory_order_relaxed);
        if (open != g.loggedOpen || standing != g.loggedStanding)
        {
            g.loggedOpen = open;
            g.loggedStanding = standing;
            const uint64_t last = g.lastNoteMs.load(std::memory_order_relaxed);
            LOG("CO-OP SAFE SHOTS automatic: %s. The session as last read: %s (%s)",
                open ? "OFF, one-machine changes are allowed"
                     : "ON, every one-machine change is stood down",
                session_read::StandingText(static_cast<session_read::Standing>(standing)),
                !last ? "never read"
                      : now >= last && now - last <= session_read::kGateStaleMs ? "fresh" : "no read for 5 s");
        }
    }
    else
        g_sessionGate.loggedOpen = -1;
    for (size_t i = 0; i < kTitleSlots; ++i)
    {
        auto& s = g_stats[i];
        const uint64_t rays = s.rays.exchange(0, std::memory_order_relaxed);
        const uint64_t traces = s.traces.exchange(0, std::memory_order_relaxed);
        const uint64_t converged = s.converged.exchange(0, std::memory_order_relaxed);
        const uint64_t written = s.written.exchange(0, std::memory_order_relaxed);
        const uint64_t declined = s.declined.exchange(0, std::memory_order_relaxed);
        const uint64_t faults = s.faults.exchange(0, std::memory_order_relaxed);
        const uint64_t muzzleAimed = s.muzzleAimed.exchange(0, std::memory_order_relaxed);
        uint64_t muzzleRefused = 0;
        char muzzleWhy[160]{};
        size_t muzzleWhyLength = 0;
        for (size_t r = 1; r < static_cast<size_t>(aim_solve::MuzzleShot::Count); ++r)
        {
            const uint64_t n = s.muzzleRefused[r].exchange(0, std::memory_order_relaxed);
            muzzleRefused += n;
            if (!n || muzzleWhyLength + 1 >= sizeof(muzzleWhy))
                continue;
            const int w = std::snprintf(muzzleWhy + muzzleWhyLength, sizeof(muzzleWhy) - muzzleWhyLength,
                                        "%s%s=%llu", muzzleWhyLength ? ", " : "",
                                        aim_solve::MuzzleShotName(static_cast<aim_solve::MuzzleShot>(r)),
                                        static_cast<unsigned long long>(n));
            if (w > 0)
                muzzleWhyLength += std::min(static_cast<size_t>(w), sizeof(muzzleWhy) - muzzleWhyLength - 1);
        }
        const uint64_t muzzleUnconverged = s.muzzleUnconverged.exchange(0, std::memory_order_relaxed);
        const uint64_t muzzleHeld = s.muzzleHeld.exchange(0, std::memory_order_relaxed);
        ReportReaims(i);
        ReportSeats(i);
#if HALOMCCVR_DEV_DIAGNOSTICS
        ReportHitClasses(i);
#endif
        {
            const int session = s.sessionType.load(std::memory_order_relaxed);
            const int players = s.sessionPlayers.load(std::memory_order_relaxed);
            const int others = s.sessionOthers.load(std::memory_order_relaxed);
            if (session != s.sessionTypeLogged || players != s.sessionPlayersLogged ||
                others != s.sessionOthersLogged)
            {
                s.sessionTypeLogged = session;
                s.sessionPlayersLogged = players;
                s.sessionOthersLogged = others;
                if (session < 0)
                    LOG("SESSION %s: no game in progress", TitleName(static_cast<GameTitle>(i)));
                else
                    LOG("SESSION %s: the game's simulation type is %d (%s), players in the game %d, machines "
                        "other than this one %d (-1: not read). A solo campaign level is a sync-server with 1 "
                        "player; a co-op joiner is a sync-client",
                        TitleName(static_cast<GameTitle>(i)), session, session_read::TypeName(session), players,
                        others);
            }
        }
        if (const uint64_t stoodDown = s.fireTimeStoodDown.exchange(0, std::memory_order_relaxed))
        {
            // The re-aims that would have applied: all of them, then on foot ([0], what the written aim's lead is
            // there to shrink) apart from seated ([1]), each with the shots that needed no re-aim.
            uint64_t count[2]{}, sum[2]{}, through[2]{};
            uint32_t peak[2]{};
            for (int k = 0; k < 2; ++k)
            {
                count[k] = s.fireTimeBends[k].count.exchange(0, std::memory_order_relaxed);
                sum[k] = s.fireTimeBends[k].sumMilliDeg.exchange(0, std::memory_order_relaxed);
                through[k] = s.fireTimeBends[k].through.exchange(0, std::memory_order_relaxed);
                peak[k] = s.fireTimeBends[k].maxMilliDeg.exchange(0, std::memory_order_relaxed);
            }
            const uint64_t bends = count[0] + count[1];
            const auto mean = [](uint64_t total, uint64_t n) { return n ? double(total) / double(n) / 1000.0 : 0.0; };
            LOG("CO-OP SAFE SHOTS %s: %llu fire-time changes stood down; each shot left as the game built it. "
                "%llu of them were re-aims that would have turned the shot by a mean of %.2f deg (max %.2f): on foot "
                "%llu (mean %.2f deg, max %.2f) beside %llu already through the point, seated %llu (mean %.2f deg, "
                "max %.2f) beside %llu",
                TitleName(static_cast<GameTitle>(i)), (unsigned long long)stoodDown, (unsigned long long)bends,
                mean(sum[0] + sum[1], bends), double((std::max)(peak[0], peak[1])) / 1000.0,
                (unsigned long long)count[0], mean(sum[0], count[0]), double(peak[0]) / 1000.0,
                (unsigned long long)through[0], (unsigned long long)count[1], mean(sum[1], count[1]),
                double(peak[1]) / 1000.0, (unsigned long long)through[1]);
        }
        ReportLead(i);
        if (muzzleAimed || muzzleRefused || muzzleUnconverged || muzzleHeld)
            LOG("SHOTS FROM THE MUZZLE %s: %llu aimed through the converged point, %llu refused%s%s%s, "
                "%llu without a converged point, %llu held off (a seat held at its stop, or a turret: the gun "
                "points elsewhere); every refusal left from the eye as before",
                TitleName(static_cast<GameTitle>(i)), (unsigned long long)muzzleAimed,
                (unsigned long long)muzzleRefused, muzzleWhyLength ? " (" : "", muzzleWhyLength ? muzzleWhy : "",
                muzzleWhyLength ? ")" : "", (unsigned long long)muzzleUnconverged,
                (unsigned long long)muzzleHeld);
        if (!rays && !traces && !converged && !declined && !faults)
            continue;
        const uint64_t hits = s.hits.exchange(0, std::memory_order_relaxed);
        const uint64_t misses = s.misses.exchange(0, std::memory_order_relaxed);
        const uint64_t cost = s.costTicks.exchange(0, std::memory_order_relaxed);
        const uint64_t costMax = s.costMaxTicks.exchange(0, std::memory_order_relaxed);
        const auto title = static_cast<GameTitle>(i);
        const float convergeWu = g_traces[i].convergeRangeWu.load(std::memory_order_relaxed);
        LOG("SIGHT %s: rays %llu, traces %llu (%llu hits, %llu misses [= the far end]), cost avg %.1f / "
            "max %.1f us; converged aim %llu polls (written, not steered: %llu), declined %llu; converging at "
            "%.2f m (smoothed); faulted=%d%s",
            TitleName(title), (unsigned long long)rays, (unsigned long long)traces, (unsigned long long)hits,
            (unsigned long long)misses, traces ? double(cost) / double(traces) * usPerTick : 0.0,
            double(costMax) * usPerTick, (unsigned long long)converged, (unsigned long long)written,
            (unsigned long long)declined,
            double(convergeWu) * double(kSightCmPerWorldUnit) * 0.01, Sight_TraceFaulted(title) ? 1 : 0,
            faults ? " (a native fault latched the trace off)" : "");
    }
}

// ---- the converge-off witness and the natural-cause counters (dev lines only; converge_policy.h) ----------
#if HALOMCCVR_DEV_DIAGNOSTICS
namespace
{
converge::ConvergeOffWitness g_convergeOff[kTitleSlots];
converge::NaturalWitness g_natural[kTitleSlots];
std::atomic<uint64_t> g_naturalHeldMs[kTitleSlots]{};
} // namespace
#endif

void Sight_NoteConvergeState(GameTitle title, bool live, converge::ConvergeOff why)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    g_convergeOff[Slot(title)].note(live, why);
#else
    (void)title; (void)live; (void)why;
#endif
}

void Sight_NoteNatural(GameTitle title, const converge::NaturalEvents& events)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    g_natural[Slot(title)].note(events);
    if (converge::NaturalHoldsAimOff(events))
        g_naturalHeldMs[Slot(title)].store(Sight_NowMs(), std::memory_order_relaxed);
#else
    (void)title; (void)events;
#endif
}

bool Sight_NaturalHeld(GameTitle title, uint64_t windowMs)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    return Fresh(g_naturalHeldMs[Slot(title)].load(std::memory_order_relaxed), Sight_NowMs(), windowMs);
#else
    (void)title; (void)windowMs;
    return false;
#endif
}

#if HALOMCCVR_DEV_DIAGNOSTICS
static void SeatLimitReport();
#endif

void Sight_ReportConverge()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    using converge::ConvergeOff;
    static converge::ConvergeEdgeLog s_edges[kTitleSlots];
    static uint64_t s_last = 0;
    const uint64_t now = Sight_NowMs();
    const auto reasonName = [](ConvergeOff why) {
        return why == ConvergeOff::None ? "Unclassified" : converge::ConvergeOffName(why);
    };
    // EDGES, every loop of the cold worker (a byte load per title; a line only on a change).
    for (size_t slot = 0; slot < kTitleSlots; ++slot)
    {
        uint8_t previous = converge::kConvergeStateNone;
        const uint8_t current = g_convergeOff[slot].current();
        if (!s_edges[slot].observe(current, now, previous))
            continue;
        const auto title = static_cast<GameTitle>(slot);
        if (converge::ConvergeStateIsOff(current))
            LOG("SIGHT %s converge-off: %s%s", TitleName(title), reasonName(converge::ConvergeStateReason(current)),
                previous == converge::kConvergeStateLive ? " (it was on)" : "");
        else
            LOG("SIGHT %s converge back on (it was off: %s)", TitleName(title),
                reasonName(converge::ConvergeStateReason(previous)));
    }
    if (now - s_last < 2000)
        return;
    s_last = now;
    SeatLimitReport();
    // SUMMARIES, every 2 s per title: why it was off (our code's reasons only), and the natural causes it stayed
    // on through.
    for (size_t slot = 0; slot < kTitleSlots; ++slot)
    {
        const auto title = static_cast<GameTitle>(slot);
        const converge::ConvergeOffCounts c = g_convergeOff[slot].take();
        const uint32_t suppressed = s_edges[slot].take_suppressed();
        // Off by configuration (F1 Converged aim, reach_direct_drive, reach_vehicle_aim) the whole window: the edge
        // line already said so.
        const bool onlyKeyOff = c.off() == c.why[static_cast<size_t>(ConvergeOff::AimOff)] && !c.offEdges &&
                                !c.onEdges;
        if ((c.off() || c.offEdges || c.onEdges) && !onlyKeyOff)
        {
            char reasons[512]{};
            size_t used = 0;
            for (size_t i = 0; i < static_cast<size_t>(ConvergeOff::Count) && used < sizeof(reasons); ++i)
            {
                if (!c.why[i])
                    continue;
                const int n = std::snprintf(reasons + used, sizeof(reasons) - used, "%s%s=%u", used ? ", " : "",
                                            reasonName(static_cast<ConvergeOff>(i)), c.why[i]);
                if (n <= 0)
                    break;
                used += static_cast<size_t>(n);
            }
            LOG("SIGHT %s converge-off %u/%u polls: %s (went off %u times, back on %u%s)", TitleName(title),
                c.off(), c.polls, used ? reasons : "none", c.offEdges, c.onEdges,
                suppressed ? "; some edge lines suppressed" : "");
        }
        const converge::NaturalCounts n = g_natural[slot].take();
        if (n.latchHeld || n.outcomeMiss || n.stall || n.turretSlew || n.reachLimited || n.yawShortH4)
            LOG("SIGHT %s natural (no switch-off) in %u polls: latchHeld=%u outcomeMiss=%u stall=%u turret=%u "
                "reachLimited=%u yawShortH4=%u (none of these switches the converged aim off)",
                TitleName(title), n.polls, n.latchHeld, n.outcomeMiss, n.stall, n.turretSlew, n.reachLimited,
                n.yawShortH4);
    }
#endif
}

// ---- AUXILIARY RAYS and their traces (sight_layer.h SightAuxRay). Seqlocks like the intent ray's: one writer each
// (the aim path, the title's trace thread), any reader. Slots of their own, so nothing the aim or the scope reads
// moves. The helpers below are shared CODE only: each user owns its own publication objects, so the player-facing
// seat limit ray (all builds) and the dev assessment ray (dev builds only, below) never share state.
namespace
{
struct AuxRayPublication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> sampleMs{0};
    std::atomic<float> origin[3]{};
    std::atomic<float> dir[3]{};
};
struct AuxTracePublication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> sampleMs{0};
    std::atomic<uint32_t> hit{0};
    std::atomic<float> rangeWu{0.0f};
};

void PublishAuxRay(AuxRayPublication& p, const SightAuxRay& ray)
{
    p.sequence.fetch_add(1, std::memory_order_acq_rel);
    p.generation.store(ray.generation, std::memory_order_relaxed);
    // Stamped HERE on the sight clock ReadAuxRay ages it against. A caller's GetTickCount64 stamp read against QPC
    // does not work: the clocks' offset (36 ms measured, and drifting) eats the 100 ms window, so the ray can
    // read never fresh.
    p.sampleMs.store(Sight_NowMs(), std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
    {
        p.origin[i].store(ray.origin[i], std::memory_order_relaxed);
        p.dir[i].store(ray.dir[i], std::memory_order_relaxed);
    }
    p.sequence.fetch_add(1, std::memory_order_release);
}

bool ReadAuxRay(const AuxRayPublication& p, uint32_t generation, uint64_t maxAgeMs, SightAuxRay& out)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = p.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        SightAuxRay r{};
        r.generation = p.generation.load(std::memory_order_relaxed);
        r.sampleMs = p.sampleMs.load(std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i)
        {
            r.origin[i] = p.origin[i].load(std::memory_order_relaxed);
            r.dir[i] = p.dir[i].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (p.sequence.load(std::memory_order_relaxed) != before)
            continue;
        if (!generation || r.generation != generation || !Fresh(r.sampleMs, Sight_NowMs(), maxAgeMs))
            return false;
        const float length = std::sqrt(r.dir[0] * r.dir[0] + r.dir[1] * r.dir[1] + r.dir[2] * r.dir[2]);
        if (!std::isfinite(length) || std::fabs(length - 1.0f) > 1.0e-2f)
            return false;
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(r.origin[i]))
                return false;
        out = r;
        return true;
    }
    return false;
}

void PublishAuxTrace(AuxTracePublication& t, uint32_t generation, bool hit, float rangeWu)
{
    t.sequence.fetch_add(1, std::memory_order_acq_rel);
    t.generation.store(generation, std::memory_order_relaxed);
    t.sampleMs.store(Sight_NowMs(), std::memory_order_relaxed);
    t.hit.store(hit ? 1u : 0u, std::memory_order_relaxed);
    t.rangeWu.store(rangeWu, std::memory_order_relaxed);
    t.sequence.fetch_add(1, std::memory_order_release);
}

bool ReadAuxTrace(const AuxTracePublication& t, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = t.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        const uint32_t g = t.generation.load(std::memory_order_relaxed);
        const uint64_t at = t.sampleMs.load(std::memory_order_relaxed);
        bool h = t.hit.load(std::memory_order_relaxed) != 0;
        float r = t.rangeWu.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (t.sequence.load(std::memory_order_relaxed) != before)
            continue;
        if (!generation || g != generation || !Fresh(at, Sight_NowMs(), maxAgeMs))
            return false;
        h = h && std::isfinite(r) && r > 0.0f;
        if (!h)
            r = kSightTraceLengthWu;   // a miss: the shot meets nothing within the whole trace
        hit = h;
        rangeWu = r;
        return true;
    }
    return false;
}

// THE SEAT LIMIT RAY's own slots (player-facing, every build).
AuxRayPublication g_limitRays[kTitleSlots];
AuxTracePublication g_limitTraces[kTitleSlots];
// THE VEHICLE CAMERA RAY's own slots (player-facing, every build).
AuxRayPublication g_cameraRays[kTitleSlots];
AuxTracePublication g_cameraTraces[kTitleSlots];
// THE VEHICLE AIM RING RAY's own slots (player-facing, every build).
AuxRayPublication g_markerRays[kTitleSlots];
AuxTracePublication g_markerTraces[kTitleSlots];

// Sight_MarkSeatLimitClamped: when each title's seat reticle was last clamped, and in which generation.
std::atomic<uint64_t> g_seatLimitClampedMs[kTitleSlots]{};
std::atomic<uint32_t> g_seatLimitClampedGeneration[kTitleSlots]{};

#if HALOMCCVR_DEV_DIAGNOSTICS
// The dev SEAT LIMIT witness: polls clamped per title, which edge, and the worst overshoot (float bits).
struct SeatLimitWitness
{
    std::atomic<uint32_t> polls{0}, yawEdge{0}, pitchEdge{0}, gunBand{0};
    std::atomic<uint32_t> maxOvershootBits{0};
};
SeatLimitWitness g_seatLimitWitness[kTitleSlots];
#endif
} // namespace

void Sight_PublishLimitRay(GameTitle title, const SightAuxRay& ray)
{
    PublishAuxRay(g_limitRays[Slot(title)], ray);
}

bool Sight_ReadLimitRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightAuxRay& out)
{
    return ReadAuxRay(g_limitRays[Slot(title)], generation, maxAgeMs, out);
}

void Sight_PublishLimitTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu)
{
    PublishAuxTrace(g_limitTraces[Slot(title)], generation, hit, rangeWu);
}

bool Sight_ReadLimitTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu)
{
    return ReadAuxTrace(g_limitTraces[Slot(title)], generation, maxAgeMs, hit, rangeWu);
}

void Sight_PublishCameraRay(GameTitle title, const SightAuxRay& ray)
{
    PublishAuxRay(g_cameraRays[Slot(title)], ray);
}

bool Sight_ReadCameraRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightAuxRay& out)
{
    return ReadAuxRay(g_cameraRays[Slot(title)], generation, maxAgeMs, out);
}

void Sight_PublishCameraTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu)
{
    PublishAuxTrace(g_cameraTraces[Slot(title)], generation, hit, rangeWu);
}

bool Sight_ReadCameraTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu)
{
    return ReadAuxTrace(g_cameraTraces[Slot(title)], generation, maxAgeMs, hit, rangeWu);
}

void Sight_PublishMarkerRay(GameTitle title, const SightAuxRay& ray)
{
    PublishAuxRay(g_markerRays[Slot(title)], ray);
}

bool Sight_ReadMarkerRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightAuxRay& out)
{
    return ReadAuxRay(g_markerRays[Slot(title)], generation, maxAgeMs, out);
}

void Sight_PublishMarkerTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu)
{
    PublishAuxTrace(g_markerTraces[Slot(title)], generation, hit, rangeWu);
}

bool Sight_ReadMarkerTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu)
{
    return ReadAuxTrace(g_markerTraces[Slot(title)], generation, maxAgeMs, hit, rangeWu);
}

void Sight_MarkSeatLimitClamped(GameTitle title)
{
    const size_t slot = Slot(title);
    g_seatLimitClampedGeneration[slot].store(TitleAdapter_GetGeneration(title), std::memory_order_relaxed);
    g_seatLimitClampedMs[slot].store(Sight_NowMs(), std::memory_order_release);
}

bool Sight_SeatLimitClampedLive(GameTitle title)
{
    const size_t slot = Slot(title);
    const uint64_t at = g_seatLimitClampedMs[slot].load(std::memory_order_acquire);
    return Fresh(at, Sight_NowMs(), 150) &&
           g_seatLimitClampedGeneration[slot].load(std::memory_order_relaxed) == TitleAdapter_GetGeneration(title);
}

void Sight_NoteSeatLimit(GameTitle title, bool yawEdge, bool pitchEdge, float overshootDeg, bool gunBand)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    auto& w = g_seatLimitWitness[Slot(title)];
    w.polls.fetch_add(1, std::memory_order_relaxed);
    if (yawEdge)
        w.yawEdge.fetch_add(1, std::memory_order_relaxed);
    if (pitchEdge)
        w.pitchEdge.fetch_add(1, std::memory_order_relaxed);
    if (gunBand)
        w.gunBand.fetch_add(1, std::memory_order_relaxed);
    if (std::isfinite(overshootDeg) && overshootDeg > 0.0f)
    {
        uint32_t seen = w.maxOvershootBits.load(std::memory_order_relaxed);
        uint32_t bits = 0;
        std::memcpy(&bits, &overshootDeg, sizeof(bits));
        for (;;)
        {
            float seenValue = 0.0f;
            std::memcpy(&seenValue, &seen, sizeof(seenValue));
            if (!(overshootDeg > seenValue) ||
                w.maxOvershootBits.compare_exchange_weak(seen, bits, std::memory_order_relaxed))
                break;
        }
    }
#else
    (void)title;
    (void)yawEdge;
    (void)pitchEdge;
    (void)overshootDeg;
    (void)gunBand;
#endif
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// Cold worker (from Sight_ReportConverge's 2 s summary): one line per title whose seat reticle clamped.
static void SeatLimitReport()
{
    for (size_t slot = 0; slot < kTitleSlots; ++slot)
    {
        auto& w = g_seatLimitWitness[slot];
        const uint32_t polls = w.polls.exchange(0, std::memory_order_relaxed);
        const uint32_t yawEdge = w.yawEdge.exchange(0, std::memory_order_relaxed);
        const uint32_t pitchEdge = w.pitchEdge.exchange(0, std::memory_order_relaxed);
        const uint32_t gunBand = w.gunBand.exchange(0, std::memory_order_relaxed);
        const uint32_t bits = w.maxOvershootBits.exchange(0, std::memory_order_relaxed);
        if (!polls)
            continue;
        float overshoot = 0.0f;
        std::memcpy(&overshoot, &bits, sizeof(overshoot));
        LOG("SEAT LIMIT %s: reticle clamped %u polls (yaw edge %u, pitch edge %u, gun band %u), max overshoot %.1f deg",
            TitleName(static_cast<GameTitle>(slot)), polls, yawEdge, pitchEdge, gunBand, overshoot);
    }
}
#endif

#if HALOMCCVR_DEV_DIAGNOSTICS
// [assess-only begin]
// ---- DEV ASSESSMENT RAY and its trace (sight_layer.h): slots of their own, separate from the seat limit ray's,
// and absent from a player build. With the assessment trace's own fault latch.
namespace
{
AuxRayPublication g_assessRays[kTitleSlots];
AuxTracePublication g_assessTraces[kTitleSlots];
std::atomic<bool> g_assessTraceFaulted[kTitleSlots]{};
std::atomic<uint32_t> g_assessTraceFaults[kTitleSlots]{};
} // namespace

void Sight_NoteAssessTraceFault(GameTitle title)
{
    g_assessTraceFaults[Slot(title)].fetch_add(1, std::memory_order_relaxed);
    if (!g_assessTraceFaulted[Slot(title)].exchange(true, std::memory_order_acq_rel))
        LOG("SIGHT %s: the ASSESSMENT trace raised a native fault; the assessment trace stays off until the title "
            "reinstalls (the aim's own trace is untouched)", TitleName(title));
}

bool Sight_AssessTraceFaulted(GameTitle title)
{
    return g_assessTraceFaulted[Slot(title)].load(std::memory_order_acquire);
}

void Sight_ResetAssessTrace(GameTitle title)
{
    g_assessTraceFaulted[Slot(title)].store(false, std::memory_order_release);
}

void Sight_PublishAssessRay(GameTitle title, const SightAssessRay& ray)
{
    PublishAuxRay(g_assessRays[Slot(title)], ray);
}

bool Sight_ReadAssessRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightAssessRay& out)
{
    return ReadAuxRay(g_assessRays[Slot(title)], generation, maxAgeMs, out);
}

void Sight_PublishAssessTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu)
{
    PublishAuxTrace(g_assessTraces[Slot(title)], generation, hit, rangeWu);
}

bool Sight_ReadAssessTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu)
{
    return ReadAuxTrace(g_assessTraces[Slot(title)], generation, maxAgeMs, hit, rangeWu);
}
// [assess-only end]
#endif
