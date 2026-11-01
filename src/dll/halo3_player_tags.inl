// PLAYER TAGS, Halo 3 and ODST (player_tags_show / _scale / _opacity / _max_distance_m; halo3_player_tags_logic.h has
// the game's side and the rules). Included in game.cpp after halo3_hud_note.inl.
//
// One hook per title, on chud_draw_navpoints: around the original, each teammate's tag gets its alpha multiplier and
// its size from the player's settings, and both are put back after it. With every setting at its default the detour
// only forwards. Halo 3 in MCC and in Project Reclaimer (which loads the same halo3.dll and enlarges the HUD block the
// navpoints live in: the draw takes them by pointer, so nothing here depends on that block's size), and ODST, whose
// draw is the same per entry (halo3_player_tags::kOdstDraw). Each title has its own lane and detour; the code is one.
//
// PROOF (resolved + guarded, nothing hardcoded): the draw by its entry signature and again by its one caller's call
// operand; the projection by the draw's own call; the view by the projection's own read; the entry layout by the
// draw's own instructions that read the position, the size, the alpha and the stride; the number of entries by the
// two fields it addresses past them (20 in MCC, 212 in Project Reclaimer, which enlarges the array). The same
// in-body checks run again on the draw's first call, after any host has patched the image, and that count is the
// one used: a miss there leaves the game's own tags for the rest of the level, said once.
//
// THREAD. The draw runs inside the HUD draw. The only fields written are the two the draw reads and the game
// rewrites every update (+0x14 alpha, +0x38 size); each is put back only where it still holds what was written, so
// an update that ran meanwhile keeps its own value. Nothing here is read by a shot, an aim or the network.

#include "../common/halo3_player_tags_logic.h"

using Halo3NavpointsDrawFn = void(__fastcall*)(uint8_t* navpoints, int user);

struct Halo3PlayerTagsLane
{
    const halo3_player_tags::DrawProfile* profile = nullptr;
    void* target = nullptr;
    Halo3NavpointsDrawFn original = nullptr;
    uintptr_t body = 0;                 // the draw, for the first-call recheck
    uintptr_t base = 0;                 // the title's DLL, for the same
    size_t size = 0;
    uint8_t* const* view = nullptr;     // the view pointer the projection reads
    std::atomic<bool> ready{false};
    std::atomic<int> checked{0};        // 0 not yet, 1 proven on the live code, -1 refused
    std::atomic<int> count{0};          // entries, decoded from the live code (written before `checked` = 1)
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint32_t> faults{0};
    // Cold reporting: the last settings the draw applied (halo3_player_tags::Fingerprint), logged once per change.
    std::atomic<uint32_t> appliedFingerprint{0};
    uint32_t reportedFingerprint = 0;
    std::atomic<uint32_t> lastPlayerTags{0}, lastHidden{0};
    // The census: each navpoint kind (KindKey) a level showed while the settings were applied, a line per new one.
    static constexpr int kKinds = 8;
    std::atomic<uint64_t> kinds[kKinds]{};
    int reportedKinds = 0;
};
Halo3PlayerTagsLane g_halo3PlayerTags;
Halo3PlayerTagsLane g_odstPlayerTags;

namespace
{
bool RemovePlayerTags(Halo3PlayerTagsLane& lane, void* detour);

// The in-body checks (halo3_player_tags_logic.h): the entry layout the writes below depend on, and the projection.
// Null when every one holds; otherwise what failed. `projection` gets the projection's address, `count` the entries.
const char* Halo3PlayerTagsBodyProof(const halo3_player_tags::DrawProfile& p, uintptr_t draw, uintptr_t base,
                                     size_t size, uintptr_t& projection, int& count)
{
    using namespace halo3_player_tags;
    const size_t span = (draw + p.drawBodyBytes <= base + size) ? p.drawBodyBytes : 0;
    if (!span)
        return "the draw's extent";
    if (!mcc_clock::FindUnique(draw, span, p.recordPattern))
        return "the record and position reads";
    if (!mcc_clock::FindUnique(draw, span, p.scaleReadPattern))
        return "the size read";
    if (!sig::Find(draw, span, p.alphaReadPattern))
        return "the alpha read";
    if (!mcc_clock::FindUnique(draw, span, p.stridePattern))
        return "the stride";
    const uintptr_t call = mcc_clock::FindUnique(draw, span, p.projectionCallPattern);
    if (!call)
        return "the projection call";
    projection = sig::RipTarget(call + p.projectionCallRel, call + p.projectionCallRel + 4);
    if (projection < base || projection >= base + size)
        return "the projection's address";
    const uintptr_t gamertag = mcc_clock::FindUnique(draw, span, p.gamertagPattern);
    if (!gamertag)
        return "the focused gamertag's read";
    uint32_t gamertagDisp = 0, focusDisp = 0;
    std::memcpy(&gamertagDisp, reinterpret_cast<const void*>(gamertag + p.gamertagDisp), sizeof(gamertagDisp));
    std::memcpy(&focusDisp, reinterpret_cast<const void*>(call + p.focusIndexDisp), sizeof(focusDisp));
    if (!(count = EntryCountFromLayout(gamertagDisp, focusDisp, p.gamertagPastEntries)))
        return "the number of entries (the gamertag and focused index disagree)";
    return nullptr;
}

// Runs the in-body checks once on the live code, from inside the draw. Guarded: no C++ object here.
bool Halo3PlayerTagsLiveProof(Halo3PlayerTagsLane& lane)
{
    const int state = lane.checked.load(std::memory_order_acquire);
    if (state)
        return state > 0;
    bool ok = false;
    int count = 0;
    __try
    {
        uintptr_t projection = 0;
        ok = !Halo3PlayerTagsBodyProof(*lane.profile, lane.body, lane.base, lane.size, projection, count);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    if (ok)
        lane.count.store(count, std::memory_order_relaxed);
    lane.checked.store(ok ? 1 : -1, std::memory_order_release);
    if (!ok)
        LOG("PLAYER TAGS %s: StockFallback: the navpoint draw changed after install (another program patched "
            "it?); teammates' tags stay as the game draws them for the rest of this level", lane.profile->title);
    else
        LOG("PLAYER TAGS %s: the navpoint draw proved on its first call: %d entries", lane.profile->title, count);
    return ok;
}

halo3_player_tags::Settings Halo3PlayerTagsSettings()
{
    halo3_player_tags::Settings s{};
    s.show = g_config.player_tags_show;
    s.scale = g_config.player_tags_scale;
    s.opacity = g_config.player_tags_opacity;
    s.maxDistanceM = g_config.player_tags_max_distance_m;
    return halo3_player_tags::Clamped(s);
}

struct Halo3PlayerTagsSaved
{
    int scanned = 0; // entries whose `changed` is set, from 0; only these are restored
    float alpha[halo3_player_tags::kMaxEntryCount];
    float scale[halo3_player_tags::kMaxEntryCount];
    float wroteAlpha[halo3_player_tags::kMaxEntryCount];
    float wroteScale[halo3_player_tags::kMaxEntryCount];
    bool changed[halo3_player_tags::kMaxEntryCount];
};

// Records a navpoint kind in the lane's census: lock-free, at most kKinds kinds, the rest uncounted.
void Halo3PlayerTagsCensus(Halo3PlayerTagsLane& lane, uint64_t key)
{
    for (auto& slot : lane.kinds)
    {
        uint64_t seen = slot.load(std::memory_order_relaxed);
        if (seen == key)
            return;
        if (!seen && (slot.compare_exchange_strong(seen, key, std::memory_order_relaxed) || seen == key))
            return;
    }
}

// Applies the settings to every player tag. Guarded: no C++ object here.
void Halo3PlayerTagsApply(Halo3PlayerTagsLane& lane, uint8_t* navpoints, const halo3_player_tags::Settings& s,
                          Halo3PlayerTagsSaved& saved)
{
    using namespace halo3_player_tags;
    uint32_t players = 0, hidden = 0;
    __try
    {
        float eye[3]{};
        bool haveEye = false;
        if (s.maxDistanceM > 0.0f && lane.view)
        {
            const uint8_t* view = *lane.view;
            if (view)
            {
                std::memcpy(eye, view + kViewPositionOffset, sizeof(eye));
                haveEye = std::isfinite(eye[0]) && std::isfinite(eye[1]) && std::isfinite(eye[2]);
            }
        }
        const int count = lane.count.load(std::memory_order_relaxed);
        for (int i = 0; i < count && i < kMaxEntryCount; ++i)
        {
            uint8_t* entry = navpoints + i * kEntryStride;
            saved.changed[i] = false;
            saved.scanned = i + 1;
            if (!entry[kValidOffset])
                continue;
            int32_t icon = 0;
            uint32_t flags = 0;
            std::memcpy(&icon, entry + kIconOffset, sizeof(icon));
            std::memcpy(&flags, entry + kFlagsOffset, sizeof(flags));
            Halo3PlayerTagsCensus(lane, KindKey(icon, flags));
            if (!IsPlayerTag(icon, flags))
                continue;
            ++players;
            float distanceM = -1.0f;
            if (haveEye)
            {
                float position[3];
                std::memcpy(position, entry + kPositionOffset, sizeof(position));
                distanceM = DistanceM(position, eye);
            }
            const Adjust adjust = AdjustFor(s, distanceM);
            if (adjust.alpha <= 0.0f)
                ++hidden;
            float alpha = 0.0f, scale = 0.0f;
            std::memcpy(&alpha, entry + kAlphaOffset, sizeof(alpha));
            std::memcpy(&scale, entry + kScaleOffset, sizeof(scale));
            saved.alpha[i] = alpha;
            saved.scale[i] = scale;
            saved.wroteAlpha[i] = alpha * adjust.alpha;
            saved.wroteScale[i] = scale * adjust.scale;
            std::memcpy(entry + kAlphaOffset, &saved.wroteAlpha[i], sizeof(float));
            std::memcpy(entry + kScaleOffset, &saved.wroteScale[i], sizeof(float));
            saved.changed[i] = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        lane.faults.fetch_add(1, std::memory_order_relaxed);
    }
    lane.lastPlayerTags.store(players, std::memory_order_relaxed);
    lane.lastHidden.store(hidden, std::memory_order_relaxed);
}

// Puts back each written field that still holds what was written. Guarded: no C++ object here.
void Halo3PlayerTagsRestore(Halo3PlayerTagsLane& lane, uint8_t* navpoints, const Halo3PlayerTagsSaved& saved)
{
    using namespace halo3_player_tags;
    __try
    {
        for (int i = 0; i < saved.scanned; ++i)
        {
            if (!saved.changed[i])
                continue;
            uint8_t* entry = navpoints + i * kEntryStride;
            float now = 0.0f;
            std::memcpy(&now, entry + kAlphaOffset, sizeof(now));
            if (now == saved.wroteAlpha[i])
                std::memcpy(entry + kAlphaOffset, &saved.alpha[i], sizeof(float));
            std::memcpy(&now, entry + kScaleOffset, sizeof(now));
            if (now == saved.wroteScale[i])
                std::memcpy(entry + kScaleOffset, &saved.scale[i], sizeof(float));
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        lane.faults.fetch_add(1, std::memory_order_relaxed);
    }
}

// The body both titles' detours run.
// The hook itself, one instantiation per title's lane. It must be a function with a stack frame: retiring the hook asks
// the unwind data where the detour's code lies, and a forwarding stub that only jumps here has none, so that check could
// never pass and the title's whole teardown would retry for good (InstallPlayerTags refuses such a detour).
template <Halo3PlayerTagsLane& Lane>
__declspec(noinline) void __fastcall PlayerTagsDetour(uint8_t* navpoints, int user)
{
    Lane.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        const halo3_player_tags::Settings settings = Halo3PlayerTagsSettings();
        const bool apply = navpoints && Lane.ready.load(std::memory_order_acquire) &&
                           !halo3_player_tags::IsStock(settings) && Halo3PlayerTagsLiveProof(Lane);
        Halo3PlayerTagsSaved saved;
        if (apply)
        {
            Halo3PlayerTagsApply(Lane, navpoints, settings, saved);
            Lane.appliedFingerprint.store(halo3_player_tags::Fingerprint(settings), std::memory_order_relaxed);
        }
        if (Lane.original)
            CallNativeWithSpareSlots(Lane.original, navpoints, user);
        if (apply)
            Halo3PlayerTagsRestore(Lane, navpoints, saved);
    }
    __finally
    {
        Lane.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// Install thread. Its own transaction: any miss leaves the game's own tags, said once.
bool InstallPlayerTags(Halo3PlayerTagsLane& lane, const halo3_player_tags::DrawProfile& p, void* detour,
                       uintptr_t base, size_t size, uint32_t generation)
{
    using namespace halo3_player_tags;
    lane.ready.store(false, std::memory_order_release);
    if (lane.target)
    {
        LOG("PLAYER TAGS %s: StockFallback: a previous navpoint hook has not retired", p.title);
        return false;
    }
    const char* why = nullptr;
    uintptr_t draw = 0, caller = 0, projection = 0, viewRead = 0;
    int count = 0;
    if (!(draw = mcc_clock::FindUnique(base, size, p.drawEntryPattern)))
        why = "the navpoint draw's entry";
    else if (!(caller = mcc_clock::FindUnique(base, size, p.callerPattern)) ||
             sig::RipTarget(caller + p.callerCallRel, caller + p.callerCallRel + 4) != draw)
        why = "the HUD draw's call of it";
    else if (const char* body = Halo3PlayerTagsBodyProof(p, draw, base, size, projection, count))
        why = body;
    else if (!(viewRead = mcc_clock::FindUnique(projection, p.projectionHeadBytes, p.viewReadPattern)))
        why = "the projection's view read";
    if (why)
    {
        LOG("PLAYER TAGS %s: StockFallback: %s did not prove; teammates' tags stay as the game draws them", p.title,
            why);
        return false;
    }
    const uintptr_t view = sig::RipTarget(viewRead + p.viewReadDisp, viewRead + p.viewReadDisp + 4);
    lane.profile = &p;
    lane.view = (view >= base && view < base + size) ? reinterpret_cast<uint8_t* const*>(view) : nullptr;
    lane.body = draw;
    lane.base = base;
    lane.size = size;
    lane.count.store(0, std::memory_order_relaxed);
    lane.checked.store(0, std::memory_order_release);
    lane.faults.store(0, std::memory_order_relaxed);
    for (auto& kind : lane.kinds)
        kind.store(0, std::memory_order_relaxed);
    lane.reportedKinds = 0;
    // Retirement finds the detour's code through its unwind entry: without one the hook could never be taken out again.
    DWORD64 detourImage = 0;
    if (!RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(detour), &detourImage, nullptr))
    {
        LOG("PLAYER TAGS %s: StockFallback: the detour has no unwind entry, so it could not be retired", p.title);
        return false;
    }
    void* const target = reinterpret_cast<void*>(draw);
    if (MH_CreateHook(target, detour, reinterpret_cast<void**>(&lane.original)) != MH_OK)
    {
        lane.original = nullptr;
        LOG("PLAYER TAGS %s: StockFallback: the navpoint draw's hook was not created", p.title);
        return false;
    }
    lane.target = target;
    lane.ready.store(true, std::memory_order_release);
    if (MH_EnableHook(target) != MH_OK)
    {
        LOG("PLAYER TAGS %s: StockFallback: the navpoint draw's hook did not enable", p.title);
        (void)RemovePlayerTags(lane, detour);
        return false;
    }
    LOG("PLAYER TAGS %s: Installed (generation %u): chud_draw_navpoints +0x%llX (called once, from +0x%llX) draws "
        "the navpoints of a user's HUD (%d here, 0x74 bytes each); around it each teammate's tag gets its alpha and "
        "size from the player tag settings and both are put back after. Projection +0x%llX, its view %s. Proven: "
        "the entry and its caller, and in the draw the position, size, alpha and stride reads and the count from the "
        "two fields past the entries (checked again on the first draw). Settings: show %d, size %.2f, opacity %.2f, "
        "max distance %.0f m (0 = none)",
        p.title, generation, static_cast<unsigned long long>(draw - base),
        static_cast<unsigned long long>(caller - base), count, static_cast<unsigned long long>(projection - base),
        lane.view ? "bound (distance limit available)" : "not bound (the distance limit does nothing)",
        g_config.player_tags_show ? 1 : 0, g_config.player_tags_scale, g_config.player_tags_opacity,
        g_config.player_tags_max_distance_m);
    return true;
}

// Worker: retire the hook (disable, prove nothing is inside, remove). Idempotent; false = try again.
bool RemovePlayerTags(Halo3PlayerTagsLane& lane, void* detour)
{
    lane.ready.store(false, std::memory_order_release);
    if (!lane.target)
        return true;
    const char* title = lane.profile ? lane.profile->title : "?";
    const auto status = MCCVR_DisableHookForRetirement(lane.target);
    if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
    {
        LOG("PLAYER TAGS %s CleanupRequired: the navpoint hook did not disable (%d)", title, static_cast<int>(status));
        return false;
    }
    // A thread that has entered the detour but not yet counted itself in `callbacks` is still found by where it stands.
    const void* functions[]{detour};
    const void* trampolines[]{reinterpret_cast<const void*>(lane.original)};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, lane.callbacks))
    {
        LOG("PLAYER TAGS %s CleanupRequired: the navpoint detour is busy", title);
        return false;
    }
    const auto removed = MH_RemoveHook(lane.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("PLAYER TAGS %s CleanupRequired: the navpoint hook was not removed (%d)", title, static_cast<int>(removed));
        return false;
    }
    lane.target = nullptr;
    lane.original = nullptr;
    lane.view = nullptr;
    lane.body = 0;
    lane.checked.store(0, std::memory_order_release);
    return true;
}

// Cold worker: one line per new navpoint kind, and one when the applied settings change, with what the last draw saw.
void ReportPlayerTags(Halo3PlayerTagsLane& lane)
{
    if (!lane.profile)
        return;
    for (; lane.reportedKinds < Halo3PlayerTagsLane::kKinds; ++lane.reportedKinds)
    {
        const uint64_t key = lane.kinds[lane.reportedKinds].load(std::memory_order_relaxed);
        if (!key)
            break;
        const int32_t icon = halo3_player_tags::KindIcon(key);
        const uint32_t flags = halo3_player_tags::KindFlags(key);
        LOG("PLAYER TAGS %s: navpoint kind icon %d flags 0x%X: %s", lane.profile->title, icon, flags,
            halo3_player_tags::IsPlayerTag(icon, flags) ? "a teammate's tag (the settings apply)"
                                                         : "an objective or carrier (left as the game draws it)");
    }
    const uint32_t applied = lane.appliedFingerprint.load(std::memory_order_relaxed);
    if (!applied || applied == lane.reportedFingerprint)
        return;
    lane.reportedFingerprint = applied;
    LOG("PLAYER TAGS %s: applied show %d, size %.2f, opacity %.2f, max distance %.0f m: last draw %u teammate "
        "tag(s), %u hidden; %u fault(s)",
        lane.profile->title, g_config.player_tags_show ? 1 : 0, g_config.player_tags_scale,
        g_config.player_tags_opacity, g_config.player_tags_max_distance_m,
        lane.lastPlayerTags.load(std::memory_order_relaxed), lane.lastHidden.load(std::memory_order_relaxed),
        lane.faults.load(std::memory_order_relaxed));
}
} // namespace

bool InstallHalo3PlayerTags(uintptr_t base, size_t size, uint32_t generation)
{
    return InstallPlayerTags(g_halo3PlayerTags, halo3_player_tags::kHalo3Draw,
                             reinterpret_cast<void*>(&PlayerTagsDetour<g_halo3PlayerTags>), base, size, generation);
}
bool RemoveHalo3PlayerTags()
{
    return RemovePlayerTags(g_halo3PlayerTags, reinterpret_cast<void*>(&PlayerTagsDetour<g_halo3PlayerTags>));
}
void ReportHalo3PlayerTags()
{
    ReportPlayerTags(g_halo3PlayerTags);
}
bool InstallOdstPlayerTags(uintptr_t base, size_t size, uint32_t generation)
{
    return InstallPlayerTags(g_odstPlayerTags, halo3_player_tags::kOdstDraw,
                             reinterpret_cast<void*>(&PlayerTagsDetour<g_odstPlayerTags>), base, size, generation);
}
bool RemoveOdstPlayerTags()
{
    return RemovePlayerTags(g_odstPlayerTags, reinterpret_cast<void*>(&PlayerTagsDetour<g_odstPlayerTags>));
}
void ReportOdstPlayerTags()
{
    ReportPlayerTags(g_odstPlayerTags);
}
