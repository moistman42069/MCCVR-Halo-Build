// SEATEXIT witness (DEV BUILDS ONLY). See seat_exit_witness.h and src/common/seat_exit_witness_logic.h.
//
// Every recorder writes a fixed ring, each slot behind its own sequence counter; nothing here allocates,
// takes a lock or logs except SeatWitness_Poll, which runs on the cold worker (~20 Hz) and prints one block
// per queued edge once that edge's +250 ms tail is in the rings.
#include "seat_exit_witness.h"

#if HALOMCCVR_DEV_DIAGNOSTICS
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include "../common/log.h"

namespace
{
using seat_witness::Sample;
using seat_witness::DriveSample;
using seat_witness::NativeActionEntry;
using seat_witness::VehicleFrameEntry;

constexpr uint64_t kHeartbeatMs = 100;   // input ring: an unchanged state is re-stored this often
constexpr uint64_t kBeforeMs = 1500;     // printed history before the edge
constexpr uint64_t kAfterMs = 250;       // printed tail after the edge (also the print delay)
constexpr uint64_t kUseWindowMs = 600;   // "was Use/Exit sent / held" window
constexpr size_t kMaxInputLines = 30;
constexpr size_t kMaxDriveLines = 12;
constexpr size_t kLastDriveCalls = 12;
constexpr uint64_t kDriveBucketMs = 250;

// A ring of trivially copyable records, one writer per slot at a time, torn reads rejected.
template<class T, unsigned N>
struct Ring
{
    struct Slot { std::atomic<uint32_t> seq{0}; T value{}; };
    Slot slots[N];
    std::atomic<uint64_t> head{0};
    void Push(const T& v) noexcept
    {
        Slot& s = slots[head.fetch_add(1, std::memory_order_acq_rel) % N];
        s.seq.fetch_add(1, std::memory_order_acq_rel);    // -> odd
        std::memcpy(&s.value, &v, sizeof(T));
        s.seq.fetch_add(1, std::memory_order_release);    // -> even
    }
    // Copies the readable slots into out (chronological by ms). Returns the count.
    size_t Snapshot(T* out) const noexcept
    {
        const uint64_t h = head.load(std::memory_order_acquire);
        const uint64_t begin = h > N ? h - N : 0;
        size_t n = 0;
        for (uint64_t k = begin; k < h; ++k)
        {
            const Slot& s = slots[k % N];
            const uint32_t before = s.seq.load(std::memory_order_acquire);
            if (before & 1u) continue;
            T copy;
            std::memcpy(&copy, &s.value, sizeof(T));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (s.seq.load(std::memory_order_relaxed) != before || !copy.ms) continue;
            size_t at = n;   // insertion: writers on several threads may interleave slightly
            while (at > 0 && out[at - 1].ms > copy.ms) { out[at] = out[at - 1]; --at; }
            out[at] = copy;
            ++n;
        }
        return n;
    }
};

Ring<Sample, 512> g_input;
Ring<DriveSample, 1024> g_drive;
Ring<NativeActionEntry, 256> g_native;
std::atomic<uint64_t> g_nativeReads[2]{};   // [0] Halo 3, [1] ODST: watched-record reads ever seen

enum EventKind : uint8_t { EventSeat, EventMode };
struct SeatEvent
{
    uint64_t ticket = 0;      // == queue index + 1 once fully written
    uint64_t ms = 0;
    uint32_t generation = 0, defIndex = 0xFFFFFFFFu;
    int32_t unit = -1, seat = -1, parent = -1, vehicleType = -1;
    uint32_t native = 0;
    int32_t prevSeat = -1, prevParent = -1, prevVehicleType = -1;
    uint32_t prevDefIndex = 0xFFFFFFFFu;
    uint64_t prevSinceMs = 0;
    seat_witness::VehicleKinematics vehicle{};
    uint8_t kind = EventSeat;
    uint8_t title = 0;
    uint8_t seated = 0;
    uint8_t modeFrom = 0, modeTo = 0;
};
constexpr unsigned kEventSlots = 16;
struct EventSlot { std::atomic<uint32_t> seq{0}; SeatEvent event{}; };
EventSlot g_events[kEventSlots];
std::atomic<uint64_t> g_eventHead{0};

void PushEvent(SeatEvent ev) noexcept
{
    const uint64_t index = g_eventHead.fetch_add(1, std::memory_order_acq_rel);
    ev.ticket = index + 1;
    EventSlot& slot = g_events[index % kEventSlots];
    slot.seq.fetch_add(1, std::memory_order_acq_rel);
    std::memcpy(&slot.event, &ev, sizeof(ev));
    slot.seq.fetch_add(1, std::memory_order_release);
}

// Camera-thread state (single writer per title).
struct SeatTrack
{
    uint32_t generation = 0;
    int8_t seated = -1;   // -1 unknown (new generation)
    int32_t seat = -1, parent = -1, vehicleType = -1;
    uint32_t defIndex = 0xFFFFFFFFu;
    uint64_t sinceMs = 0;
    VehicleFrameEntry frames[256]{};
    unsigned frameHead = 0;
};
SeatTrack g_track[8];

// Native-reader state (the game thread that runs the player-control builder).
struct NativeWatch { uint8_t frames = 0xFF; uint64_t storedMs = 0; };
NativeWatch g_nativeWatch[2][2];   // [title slot][0 = Use, 1 = Reload consumer]

const char* TitleName(uint8_t title)
{
    switch (static_cast<GameTitle>(title))
    {
    case GameTitle::Halo3: return "Halo 3";
    case GameTitle::Halo3ODST: return "ODST";
    case GameTitle::HaloReach: return "Reach";
    case GameTitle::Halo4: return "Halo 4";
    case GameTitle::Halo2: return "Halo 2";
    case GameTitle::HaloCE: return "Halo CE";
    default: return "?";
    }
}
const char* ModeName(uint8_t mode)
{
    static constexpr const char* names[]{"shell","loading","gameplay","paused","cutscene","vehicle","turret",
        "dead","unsupported"};
    return mode < sizeof(names) / sizeof(names[0]) ? names[mode] : "?";
}
int DriveSlot(uint8_t title)
{
    const auto t = static_cast<GameTitle>(title);
    return t == GameTitle::Halo3 ? 0 : t == GameTitle::Halo3ODST ? 1 : t == GameTitle::Halo4 ? 2 : -1;
}
unsigned ReloadConsumerAction(uint8_t title)
{
    return static_cast<GameTitle>(title) == GameTitle::Halo3ODST ? 0x2Au : 0x26u;
}

// Cold-worker scratch (static: no allocation, worker thread only).
Sample g_inputSnap[512];
DriveSample g_driveSnap[1024];
NativeActionEntry g_nativeSnap[256];
size_t g_lineIndex[kMaxInputLines];
uint64_t g_witnessCount = 0;
uint64_t g_eventTail = 0;
uint64_t g_lastExitMs[8]{}, g_lastExitId[8]{};

void PrintNative(uint64_t id, const SeatEvent& ev, size_t count, unsigned action, const char* what)
{
    const int titleSlot = static_cast<GameTitle>(ev.title) == GameTitle::Halo3 ? 0
        : static_cast<GameTitle>(ev.title) == GameTitle::Halo3ODST ? 1 : -1;
    if (titleSlot < 0)
    {
        LOG("SEATEXIT #%llu game %s record: not wired for %s", id, what, TitleName(ev.title));
        return;
    }
    const uint64_t ever = g_nativeReads[titleSlot].load(std::memory_order_relaxed);
    const auto span = seat_witness::EvaluateNativeHeld(g_nativeSnap, count, ev.title,
        static_cast<uint8_t>(action), ev.ms, kUseWindowMs);
    if (!ever)
        LOG("SEATEXIT #%llu game %s record (native 0x%X): NO player-control reads observed this session -- the "
            "action bridge's reader hook is not live (bridge off, not admitted, or retired), so the game's own "
            "button state cannot be shown", id, what, action);
    else if (!span.covered && !span.reads)
        LOG("SEATEXIT #%llu game %s record (native 0x%X): no reads in the %llu ms before the edge (%llu ever)",
            id, what, action, kUseWindowMs, ever);
    else if (span.any)
        LOG("SEATEXIT #%llu game %s record (native 0x%X): HELD %lld..%lld ms before the edge (max %u frames, "
            "%u ms) -- the GAME saw this button down", id, what, action, static_cast<long long>(span.firstMs),
            static_cast<long long>(span.lastMs), span.maxFrames, span.maxHeldMs);
    else
        LOG("SEATEXIT #%llu game %s record (native 0x%X): NOT held in the %llu ms before the edge (%zu entries "
            "in window, %llu reads ever)", id, what, action, kUseWindowMs, span.reads, ever);
}

void PrintDrive(uint64_t id, const SeatEvent& ev, size_t count)
{
    const int slot = DriveSlot(ev.title);
    if (slot < 0) return;
    const uint64_t from = ev.ms > kBeforeMs ? ev.ms - kBeforeMs : 0, to = ev.ms + kAfterMs;
    seat_witness::DriveBucket buckets[8];
    const size_t nb = seat_witness::SummarizeDrive(g_driveSnap, count, static_cast<uint8_t>(slot), from, to,
        kDriveBucketMs, buckets, 8);
    uint32_t total = 0;
    for (size_t b = 0; b < nb; ++b)
        for (unsigned c = 0; c < seat_witness::kDriveCallerCount; ++c) total += buckets[b].byCaller[c];
    if (!total)
    {
        LOG("SEATEXIT #%llu drive: no direct-drive accumulator calls recorded in %lld..+%llu ms (hook not "
            "installed/running for %s)", id, -static_cast<long long>(kBeforeMs), kAfterMs, TitleName(ev.title));
        return;
    }
    LOG("SEATEXIT #%llu drive (%s seat/foot direct drive, sim thread; deg; drift = record at a stick call vs "
        "our previous write; aim = the game's aim the input thread read):", id, TitleName(ev.title));
    for (size_t b = 0; b < nb; ++b)
    {
        const auto& k = buckets[b];
        char outcomes[160];
        size_t len = 0;
        outcomes[0] = 0;
        for (unsigned o = 1; o < seat_witness::DriveOutcomeCount; ++o)
            if (k.byOutcome[o])
            {
                char part[40];
                std::snprintf(part, sizeof(part), "%s%s=%u", len ? " " : "", seat_witness::kDriveOutcomeNames[o],
                    k.byOutcome[o]);
                len = seat_witness::Append(outcomes, sizeof(outcomes), len, part);
            }
        LOG("SEATEXIT #%llu drive %+5lld..%+5lld: stick %u kick %u no-unit %u other %u | %s | writes y%u p%u | "
            "wrote-aim y%.2f p%.2f | drift y%.2f p%.2f | record-aim y%.2f p%.2f%s",
            id, static_cast<long long>(k.fromMs) - static_cast<long long>(ev.ms),
            static_cast<long long>(k.toMs) - static_cast<long long>(ev.ms),
            k.byCaller[0], k.byCaller[3], k.byCaller[4], k.byCaller[5] + k.byCaller[1] + k.byCaller[2],
            len ? outcomes : "-", k.yawWrites, k.pitchWrites, k.maxWroteVsAimYaw, k.maxWroteVsAimPitch,
            k.maxDriftYaw, k.maxDriftPitch, k.maxNativeVsAimYaw, k.maxNativeVsAimPitch,
            k.pcChanges ? " RECORD CHANGED" : "");
        if (k.haveWroteYaw || k.haveNativeYaw)
            LOG("SEATEXIT #%llu drive %+5lld        raw yaw: wrote %+.1f..%+.1f record %+.1f..%+.1f aim %+.1f..%+.1f | "
                "raw pitch wrote %+.1f..%+.1f | branch jumps %u, >30deg steps %u",
                id, static_cast<long long>(k.fromMs) - static_cast<long long>(ev.ms),
                k.minWroteYaw, k.maxWroteYaw, k.minNativeYaw, k.maxNativeYaw, k.minAimYaw, k.maxAimYaw,
                k.minWrotePitch, k.maxWrotePitch, k.branchJumps, k.bigSteps);
    }
    // State changes (caller kind, outcome, flags, axes, record, seat key), compressed like the input lines.
    // Only this title's calls take part.
    static DriveSample mine[1024];
    size_t n = 0;
    for (size_t i = 0; i < count; ++i)
        if (g_driveSnap[i].slot == slot) mine[n++] = g_driveSnap[i];
    size_t idx[kMaxDriveLines], omitted = 0;
    const size_t lines = seat_witness::CompressBy(mine, n, from, to, idx, kMaxDriveLines, &omitted,
        [](const DriveSample& a, const DriveSample& b) { return seat_witness::DriveSameState(a, b); });
    LOG("SEATEXIT #%llu drive state changes: %zu shown, %zu older omitted", id, lines, omitted);
    char line[384];
    for (size_t i = 0; i < lines; ++i)
    {
        const DriveSample& s = mine[idx[i]];
        size_t len = 0;
        char head[160];
        std::snprintf(head, sizeof(head), "%+6lldms %-7s %-15s rec %08X key %08X want%s%s wrote%s%s [",
            static_cast<long long>(s.ms) - static_cast<long long>(ev.ms),
            s.caller < seat_witness::kDriveCallerCount ? seat_witness::kDriveCallerNames[s.caller] : "?",
            s.outcome < seat_witness::DriveOutcomeCount ? seat_witness::kDriveOutcomeNames[s.outcome] : "?",
            s.pcLow, s.seatKey, (s.axes & 1) ? "Y" : "", (s.axes & 2) ? "P" : "", (s.axes & 0x10) ? "Y" : "",
            (s.axes & 0x20) ? "P" : "");
        len = seat_witness::Append(line, sizeof(line), 0, head);
        bool any = false;
        for (unsigned f = 0; f < seat_witness::kDriveFlagCount; ++f)
            if (s.flags & (1u << f))
            {
                if (any) len = seat_witness::Append(line, sizeof(line), len, " ");
                len = seat_witness::Append(line, sizeof(line), len, seat_witness::kDriveFlagNames[f]);
                any = true;
            }
        len = seat_witness::Append(line, sizeof(line), len, "]");
        if (s.outcome == seat_witness::DrivePutBackRefused)
        {
            char r[24];
            std::snprintf(r, sizeof(r), " refusal %u", s.refusal);
            seat_witness::Append(line, sizeof(line), len, r);
        }
        LOG("SEATEXIT #%llu drive %s", id, line);
    }
    // The last calls before the edge (every caller: a put-back writes too), numerically, raw degrees.
    size_t lastIdx[kLastDriveCalls], found = 0;
    for (size_t i = n; i-- > 0 && found < kLastDriveCalls;)
        if (mine[i].ms <= ev.ms) lastIdx[found++] = i;
    for (size_t k = found; k-- > 0;)
    {
        const DriveSample& s = mine[lastIdx[k]];
        const float r2d = 57.2957795f;
        LOG("SEATEXIT #%llu drive call %+6lldms %-7s %-15s rec %08X record(%+8.2f,%+7.2f) wrote(%+8.2f,%+7.2f) target(%+8.2f,%+7.2f) "
            "aim(%+8.2f,%+7.2f) pub age %ums",
            id, static_cast<long long>(s.ms) - static_cast<long long>(ev.ms),
            s.caller < seat_witness::kDriveCallerCount ? seat_witness::kDriveCallerNames[s.caller] : "?",
            s.outcome < seat_witness::DriveOutcomeCount ? seat_witness::kDriveOutcomeNames[s.outcome] : "?",
            s.pcLow, s.nativeYaw * r2d, s.nativePitch * r2d, s.wroteYaw * r2d, s.wrotePitch * r2d, s.setYaw * r2d,
            s.setPitch * r2d, s.aimYaw * r2d, s.aimPitch * r2d, static_cast<unsigned>(s.pubAgeMs));
    }
}

void PrintVehicle(uint64_t id, const SeatEvent& ev)
{
    const auto& v = ev.vehicle;
    if (static_cast<GameTitle>(ev.title) != GameTitle::Halo3)
    {
        LOG("SEATEXIT #%llu vehicle frame: not captured for %s (only Halo 3's sampler reads a proven object "
            "frame)", id, TitleName(ev.title));
        return;
    }
    if (!v.valid)
    {
        LOG("SEATEXIT #%llu vehicle frame: none in the %llu ms before the edge (no validated object frame: "
            "mounted/too-deep chain or failed sanity gate)", id, kBeforeMs);
        return;
    }
    const float z = v.up[2] > 1 ? 1 : (v.up[2] < -1 ? -1 : v.up[2]);
    const float minZ = v.minUpZ > 1 ? 1 : (v.minUpZ < -1 ? -1 : v.minUpZ);
    LOG("SEATEXIT #%llu vehicle frame (last at %lld ms, %u frames in %llu ms): up(%+.2f,%+.2f,%+.2f) tilt %.0f deg, "
        "worst tilt %.0f deg%s; speed %.2f wu/s (~%.1f m/s), max %.2f wu/s",
        id, static_cast<long long>(v.lastMs), v.frames, kBeforeMs, v.up[0], v.up[1], v.up[2],
        std::acos(z) * 57.2957795f, std::acos(minZ) * 57.2957795f, v.minUpZ < 0 ? " -- UPSIDE DOWN" : "",
        v.speed, v.speed * 3.048f, v.maxSpeed);
}

void PrintInput(uint64_t id, const SeatEvent& ev, size_t count, uint64_t oldest)
{
    const GameTitle gameTitle = static_cast<GameTitle>(ev.title);
    const uint32_t useActions = seat_witness::UseActions(gameTitle);
    const unsigned useNative = vr_mapping::NativeAction(gameTitle, vr_mapping::Interact);
    const auto verdict = seat_witness::EvaluateUse(g_inputSnap, count, ev.ms, kUseWindowMs, useActions);
    char names[160];
    seat_witness::AppendActions(names, sizeof(names), 0, useActions);
    char mask[96];
    seat_witness::AppendTransport(mask, sizeof(mask), 0, verdict.useMask);
    if (verdict.sent)
    {
        char subs[128], srcs[128];
        seat_witness::AppendSubsystems(subs, sizeof(subs), 0, verdict.subsystems);
        seat_witness::AppendSources(srcs, sizeof(srcs), 0, verdict.sources);
        LOG("SEATEXIT #%llu our input: Use/Exit (native %u = %s) WAS SENT %lld..%lld ms before the edge, via %s; "
            "raw X %s; sources down [%s]; useMask[%s]",
            id, useNative, names, static_cast<long long>(verdict.firstMs), static_cast<long long>(verdict.lastMs),
            subs, verdict.rawX ? "DOWN" : "up", srcs, verdict.known ? mask : "unknown");
    }
    else
    {
        char other[160], fed[96];
        seat_witness::AppendActions(other, sizeof(other), 0, verdict.otherActions);
        seat_witness::AppendTransport(fed, sizeof(fed), 0, verdict.otherFed);
        LOG("SEATEXIT #%llu our input: Use/Exit (native %u = %s) NOT sent in the %llu ms before the edge%s; "
            "useMask[%s]; other actions [%s]; other fed [%s]",
            id, useNative, names, kUseWindowMs, verdict.covered ? "" : " (ring does not cover the whole window)",
            verdict.known ? mask : "unknown: bindings not read", other, fed);
    }
    size_t omitted = 0;
    const uint64_t from = ev.ms > kBeforeMs ? ev.ms - kBeforeMs : 0;
    const size_t lines = seat_witness::Compress(g_inputSnap, count, from, ev.ms + kAfterMs,
        g_lineIndex, kMaxInputLines, &omitted);
    LOG("SEATEXIT #%llu input %lld..+%llu ms: %zu state(s) shown, %zu older change(s) omitted, ring reaches "
        "%lld ms (%zu samples)",
        id, -static_cast<long long>(kBeforeMs), kAfterMs, lines, omitted,
        oldest ? static_cast<long long>(oldest) - static_cast<long long>(ev.ms) : 0ll, count);
    char line[512];
    for (size_t i = 0; i < lines; ++i)
    {
        seat_witness::FormatSample(line, sizeof(line), g_inputSnap[g_lineIndex[i]], ev.ms);
        LOG("SEATEXIT #%llu %s", id, line);
    }
}

void PrintBlock(const SeatEvent& ev)
{
    const uint8_t t = ev.title < 8 ? ev.title : 0;
    if (ev.kind == EventMode && g_lastExitMs[t] && ev.ms >= g_lastExitMs[t] && ev.ms - g_lastExitMs[t] <= 2000)
    {
        LOG("SEATEXIT %s runtime mode %s -> %s at +%llu ms after raw seat edge #%llu (no separate block)",
            TitleName(ev.title), ModeName(ev.modeFrom), ModeName(ev.modeTo), ev.ms - g_lastExitMs[t],
            g_lastExitId[t]);
        return;
    }
    const uint64_t id = ++g_witnessCount;
    const char* title = TitleName(ev.title);
    if (ev.kind == EventMode)
        LOG("SEATEXIT #%llu %s: runtime mode %s -> %s with NO raw seat edge in the 2 s before (raw seat "
            "sampler not running or did not see it)", id, title, ModeName(ev.modeFrom), ModeName(ev.modeTo));
    else if (ev.seated)
        LOG("SEATEXIT #%llu %s: ENTERED seat %d (vehicle type %d def 0x%04X parent 0x%08X nativeInVehicle=%u, "
            "unit 0x%08X) after %llu ms on foot [raw seated edge, camera thread]",
            id, title, ev.seat, ev.vehicleType, ev.defIndex & 0xFFFFu, static_cast<unsigned>(ev.parent),
            ev.native, static_cast<unsigned>(ev.unit),
            ev.prevSinceMs && ev.ms > ev.prevSinceMs ? ev.ms - ev.prevSinceMs : 0ull);
    else
        LOG("SEATEXIT #%llu %s: EXITED seat %d (vehicle type %d def 0x%04X parent 0x%08X) after %llu ms seated; "
            "now unit 0x%08X seat %d parent 0x%08X nativeInVehicle=%u%s [raw seated edge, camera thread]",
            id, title, ev.prevSeat, ev.prevVehicleType, ev.prevDefIndex & 0xFFFFu,
            static_cast<unsigned>(ev.prevParent),
            ev.prevSinceMs && ev.ms > ev.prevSinceMs ? ev.ms - ev.prevSinceMs : 0ull,
            static_cast<unsigned>(ev.unit), ev.seat, static_cast<unsigned>(ev.parent), ev.native,
            ev.unit == -1 ? " (NO PLAYER UNIT: death/respawn/load)" : "");
    if (ev.kind == EventSeat && !ev.seated)
    {
        g_lastExitMs[t] = ev.ms;
        g_lastExitId[t] = id;
    }

    const size_t inputCount = g_input.Snapshot(g_inputSnap);
    const size_t driveCount = g_drive.Snapshot(g_driveSnap);
    const size_t nativeCount = g_native.Snapshot(g_nativeSnap);
    if (ev.kind == EventSeat && !ev.seated) PrintVehicle(id, ev);
    PrintNative(id, ev, nativeCount, 3u, "Use/Exit");
    PrintNative(id, ev, nativeCount, ReloadConsumerAction(ev.title), "Reload");
    PrintDrive(id, ev, driveCount);
    PrintInput(id, ev, inputCount, inputCount ? g_inputSnap[0].ms : 0);
    LOG("SEATEXIT #%llu end", id);
}
}

uint64_t SeatWitness_NowMs() noexcept
{
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart ? f.QuadPart : 1;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart / frequency) * 1000ull +
        static_cast<uint64_t>((now.QuadPart % frequency) * 1000 / frequency);
}

void SeatWitness_Record(const Sample& sample) noexcept
{
    thread_local Sample last{};
    thread_local bool haveLast = false;
    if (haveLast && seat_witness::SameState(sample, last) && sample.ms >= last.ms &&
        sample.ms - last.ms < kHeartbeatMs)
        return;
    last = sample;
    haveLast = true;
    g_input.Push(sample);
}

void SeatWitness_RecordDrive(const DriveSample& sample) noexcept
{
    g_drive.Push(sample);
}

void SeatWitness_NoteNativeAction(GameTitle title, unsigned action, bool present, uint8_t frames,
    uint16_t heldMs) noexcept
{
    const int t = title == GameTitle::Halo3 ? 0 : title == GameTitle::Halo3ODST ? 1 : -1;
    if (t < 0) return;
    const int a = action == 3u ? 0 : action == ReloadConsumerAction(static_cast<uint8_t>(title)) ? 1 : -1;
    if (a < 0) return;
    g_nativeReads[t].fetch_add(1, std::memory_order_relaxed);
    NativeWatch& w = g_nativeWatch[t][a];
    const uint64_t now = SeatWitness_NowMs();
    const uint8_t f = present ? frames : 0;
    const bool toggled = w.frames == 0xFF || (w.frames != 0) != (f != 0);
    // While held, also store each new frame count, so a short press shows its length.
    const bool grew = f && f != w.frames;
    if (!toggled && !grew && now >= w.storedMs && now - w.storedMs < 250)
        return;
    NativeActionEntry e{};
    e.ms = now;
    e.heldMs = present ? heldMs : 0;
    e.frames = f;
    e.action = static_cast<uint8_t>(action);
    e.title = static_cast<uint8_t>(title);
    e.present = present ? 1 : 0;
    e.heartbeat = (!toggled && !grew) ? 1 : 0;
    w.frames = f;
    w.storedMs = now;
    g_native.Push(e);
}

void SeatWitness_NoteVehicleFrame(GameTitle title, const float pos[3], const float fwd[3],
    const float up[3]) noexcept
{
    const unsigned index = static_cast<unsigned>(title);
    if (index >= 8 || !pos || !fwd || !up) return;
    SeatTrack& track = g_track[index];
    VehicleFrameEntry& f = track.frames[track.frameHead++ % 256];
    f.ms = SeatWitness_NowMs();
    for (int i = 0; i < 3; ++i) { f.pos[i] = pos[i]; f.fwd[i] = fwd[i]; f.up[i] = up[i]; }
}

void SeatWitness_NoteSeat(GameTitle title, uint32_t generation, bool seated, int unit, int seat,
    int parent, int vehicleType, uint32_t defIndex, unsigned nativeInVehicle) noexcept
{
    const unsigned index = static_cast<unsigned>(title);
    if (index >= 8) return;
    SeatTrack& track = g_track[index];
    const uint64_t now = SeatWitness_NowMs();
    if (track.generation != generation)
    {
        track.generation = generation;
        track.seated = -1;
        track.seat = track.parent = track.vehicleType = -1;
        track.defIndex = 0xFFFFFFFFu;
        track.sinceMs = 0;
    }
    const int8_t next = seated ? 1 : 0;
    if (track.seated == next)
        return;
    if (track.seated >= 0)
    {
        SeatEvent ev{};
        ev.kind = EventSeat;
        ev.ms = now;
        ev.generation = generation;
        ev.title = static_cast<uint8_t>(title);
        ev.seated = static_cast<uint8_t>(next);
        ev.unit = unit; ev.seat = seat; ev.parent = parent;
        ev.vehicleType = vehicleType; ev.defIndex = defIndex; ev.native = nativeInVehicle;
        ev.prevSeat = track.seat; ev.prevParent = track.parent;
        ev.prevVehicleType = track.vehicleType; ev.prevDefIndex = track.defIndex;
        ev.prevSinceMs = track.sinceMs;
        if (!seated)
        {
            // The frame ring is this thread's own: order it oldest-first into a local copy (camera thread,
            // once per edge: 256 x 44 bytes on the stack).
            VehicleFrameEntry ordered[256];
            const unsigned head = track.frameHead;
            const unsigned n = head < 256 ? head : 256;
            for (unsigned i = 0; i < n; ++i) ordered[i] = track.frames[(head - n + i) % 256];
            ev.vehicle = seat_witness::SummarizeVehicle(ordered, n, now, kBeforeMs);
        }
        PushEvent(ev);
    }
    track.seated = next;
    track.seat = seat; track.parent = parent; track.vehicleType = vehicleType; track.defIndex = defIndex;
    track.sinceMs = now;
}

void SeatWitness_NoteRuntimeMode(GameTitle title, uint8_t previous, uint8_t next) noexcept
{
    const bool fromSeat = previous == static_cast<uint8_t>(RuntimeMode::Vehicle) ||
        previous == static_cast<uint8_t>(RuntimeMode::Turret);
    if (!fromSeat || next != static_cast<uint8_t>(RuntimeMode::Gameplay))
        return;
    SeatEvent ev{};
    ev.kind = EventMode;
    ev.ms = SeatWitness_NowMs();
    ev.title = static_cast<uint8_t>(title);
    ev.modeFrom = previous;
    ev.modeTo = next;
    PushEvent(ev);
}

void SeatWitness_Poll() noexcept
{
    static bool announced = false;
    if (!announced)
    {
        announced = true;
        LOG("SEATEXIT witness: DEV build. On every raw Halo 3 / ODST seat edge (and any vehicle->gameplay mode "
            "edge without one) prints: the vehicle frame (Halo 3), the game's own Use/Reload records, the seat "
            "direct drive's calls, and our input for the last %llu ms (changes only)", kBeforeMs);
    }
    const uint64_t head = g_eventHead.load(std::memory_order_acquire);
    if (head - g_eventTail > kEventSlots)
    {
        LOG("SEATEXIT witness: %llu edge(s) dropped (queue overflow)", head - g_eventTail - kEventSlots);
        g_eventTail = head - kEventSlots;
    }
    const uint64_t now = SeatWitness_NowMs();
    while (g_eventTail < head)
    {
        EventSlot& slot = g_events[g_eventTail % kEventSlots];
        const uint32_t before = slot.seq.load(std::memory_order_acquire);
        if (before & 1u) return;   // being written: next poll
        SeatEvent ev;
        std::memcpy(&ev, &slot.event, sizeof(ev));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.seq.load(std::memory_order_relaxed) != before) return;
        if (ev.ticket != g_eventTail + 1) return;   // claimed but not written yet
        if (now < ev.ms + kAfterMs) return;         // wait for the tail
        ++g_eventTail;
        PrintBlock(ev);
    }
}
#endif
