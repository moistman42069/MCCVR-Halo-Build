// Included in game.cpp (inside its anonymous namespace) after halo3_head_relative.inl. Installed and retired with the
// Halo 3 core (InstallHook, RemoveInstalledGameHooks); reported by the cold worker (Halo3TrackerHudHeightReport).
//
// THE MOTION TRACKER'S BLIPS MOVE WITH THE HUD HEIGHT, Halo 3 (src/common/halo3_tracker_hud_height_logic.h has the
// mechanism, the signatures and the arithmetic; docs/HALO3-TRACKER-HUD-HEIGHT-2026-10-07.md the evidence).
//
// The HUD height hook (game.cpp, HudAnchorBasisHook) lowers the tracker's ART (once, like every HUD element), and
// cannot reach its BLIPS: the blip draw (halo3 +0x2F3904) places them around a centre it reads from the HUD's frame
// globals. Left alone, every dot (the player's own included) is drawn below the tracker. One detour on that draw
// lowers the centre's y by what the art was lowered in this HUD pass, calls the game's own draw and puts the game's
// y back.
//
//   * WHAT IT CANNOT TOUCH. One float of the HUD's frame globals, for the length of one draw call, restored
//     before the detour returns (also when the game's draw faults). The HUD frame setup rewrites it at the start
//     of every HUD pass anyway. Not the tracker's contacts, nothing the simulation, the aim or the network reads.
//   * HOW MUCH comes from the anchor hook itself (t_halo3HudHeightLowered: same thread, same HUD pass), so there
//     is no second copy of its conditions to drift. No anchor hook, a height of 0, a flat session: nothing is
//     written.
//   * EVERY REFUSAL IS THE GAME'S OWN CENTRE, counted by reason (halo3_tracker_hud_height::Use). The game's y
//     goes back only where our value still stands; a centre the game rewrote meanwhile is left alone and counted.
//   * PROOF (addrcascade: resolved + guarded, nothing hardcoded): five signatures, each unique in the image; the
//     hook target proven twice (its own entry and the operand of its one call); the centre's address decoded
//     from three sites that must agree (the draw's read, the contact builder's read, the frame setup's write),
//     each with the scale pair right behind it; and the page must be the title's own committed, writable data.
//     Any miss: StockFallback, one line.
//   * LTCG: `py -3 tools/audit_caller_area.py halo3.dll 2F3904:8:v` is clean (no caller-area slot touched; one
//     native caller, +0x2D2A6E, which keeps no volatile register across the call and reads no return value); the
//     detour forwards through CallNativeWithSpareSlots.
// Cost per HUD pass: one thread-local read, two float operations, two guarded writes. No allocation, no logging.

using Halo3TrackerBlipDrawFn = void(__fastcall*)(void* state);

struct Halo3TrackerHudHeightLane
{
    // The hook (worker-owned; the detour reads `ready`, `origin` and the original).
    void* target = nullptr;
    Halo3TrackerBlipDrawFn original = nullptr;
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> ready{false};
    float* origin = nullptr;   // the tracker's centre on the HUD: x, y, then the scale pair (decoded at install)
    // Verdicts, counted where the HUD is drawn; the cold worker reads and clears them.
    std::atomic<uint32_t> uses[static_cast<size_t>(halo3_tracker_hud_height::Use::Count)]{};
    std::atomic<uint32_t> faults{0};
    std::atomic<uint32_t> rewritten{0};   // draws after which the game's y was NOT put back: not our value any more
    // The last draw that was moved: the height the anchor hook applied, the game's centre and the y it was given.
    std::atomic<float> lastHeight{0.0f}, lastOriginX{0.0f}, lastOriginY{0.0f}, lastLoweredY{0.0f};
#if HALOMCCVR_DEV_DIAGNOSTICS
    // IS IT THE CENTRE (dev)? The scale pair behind it, and the pair of the HUD-resolution record it is copied
    // from (they differ only where MCC's own HUD options moved the tracker).
    const uint8_t* const* record = nullptr;
    std::atomic<float> lastScale[2]{}, lastRecord[2]{};
#endif
    // Cold worker only.
    int reportedSwitch = -1;
    int reportedState = -1;
    uint64_t reportMs = 0;
};
Halo3TrackerHudHeightLane g_halo3TrackerHudHeight;

namespace
{
// Before the game draws the blips: give their centre the y the tracker's art has. True = the centre now holds
// `ours` and `saved` has to go back. Guarded: the centre is a static of the title.
bool Halo3TrackerOriginLower(Halo3TrackerHudHeightLane& lane, float& saved, float& ours)
{
    namespace t = halo3_tracker_hud_height;
    // What the anchor hook lowered the HUD's art by since the last blip draw on this thread: consumed here.
    const float height = t_halo3HudHeightLowered;
    t_halo3HudHeightLowered = 0.0f;
    t::Use use = t::Use::SwitchOff;
    bool lowered = false;
    if (lane.ready.load(std::memory_order_acquire) && g_config.tracker_blips_follow_hud_height)
    {
        __try
        {
            float* const origin = lane.origin;
            saved = origin[1];
            use = t::LoweredOriginY(true, height, saved, ours);
            if (use == t::Use::Lowered)
            {
                lane.lastHeight.store(height, std::memory_order_relaxed);
                lane.lastOriginX.store(origin[0], std::memory_order_relaxed);
                lane.lastOriginY.store(saved, std::memory_order_relaxed);
                lane.lastLoweredY.store(ours, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
                // ADDR-HYGIENE: dev-only (diagnostic reads, guarded). The scale pair is the two floats behind the
                // centre (the install proves that at three sites); the record's pair is at +0x18, a byte of the
                // frame setup's own pattern (mov rcx,[rax+0x18]).
                lane.lastScale[0].store(origin[2], std::memory_order_relaxed);
                lane.lastScale[1].store(origin[3], std::memory_order_relaxed);
                if (const uint8_t* const record = lane.record ? *lane.record : nullptr)
                {
                    float pair[2];
                    memcpy(pair, record + 0x18, sizeof(pair));
                    lane.lastRecord[0].store(pair[0], std::memory_order_relaxed);
                    lane.lastRecord[1].store(pair[1], std::memory_order_relaxed);
                }
#endif
                // The write is the last thing that can fault: nothing after it may leave `ours` behind.
                origin[1] = ours;
                lowered = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            lane.faults.fetch_add(1, std::memory_order_relaxed);
            use = t::Use::Unreadable;
        }
    }
    lane.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
    return lowered;
}

// After the game's draw: its own y goes back where our value still stands.
void Halo3TrackerOriginRestore(Halo3TrackerHudHeightLane& lane, float saved, float ours)
{
    __try
    {
        float* const y = lane.origin + 1;
        if (halo3_tracker_hud_height::RestoreOurs(*y, ours))
            *y = saved;
        else
            lane.rewritten.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        lane.faults.fetch_add(1, std::memory_order_relaxed);
    }
}
} // namespace

__declspec(noinline) void __fastcall Halo3TrackerBlipDrawDetour(void* state)
{
    auto& lane = g_halo3TrackerHudHeight;
    lane.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        float saved = 0.0f, ours = 0.0f;
        const bool lowered = Halo3TrackerOriginLower(lane, saved, ours);
        __try
        {
            if (lane.original)
                CallNativeWithSpareSlots(lane.original, state);
        }
        __finally
        {
            if (lowered)
                Halo3TrackerOriginRestore(lane, saved, ours);
        }
    }
    __finally
    {
        lane.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

// Worker: retire the hook (disable, prove nothing is inside, remove). Idempotent; false = try again.
bool RemoveHalo3TrackerHudHeight()
{
    auto& lane = g_halo3TrackerHudHeight;
    lane.ready.store(false, std::memory_order_release);
    if (!lane.target)
        return true;
    const auto status = MCCVR_DisableHookForRetirement(lane.target);
    if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
    {
        LOG("TRACKER HUD HEIGHT Halo 3 CleanupRequired: the blip-draw hook did not disable (%d)",
            static_cast<int>(status));
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo3TrackerBlipDrawDetour)};
    const void* trampolines[]{reinterpret_cast<const void*>(lane.original)};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, lane.callbacks))
    {
        LOG("TRACKER HUD HEIGHT Halo 3 CleanupRequired: the blip-draw detour is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(lane.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("TRACKER HUD HEIGHT Halo 3 CleanupRequired: the blip-draw hook was not removed (%d)",
            static_cast<int>(removed));
        return false;
    }
    lane.target = nullptr;
    lane.original = nullptr;
    lane.origin = nullptr;
#if HALOMCCVR_DEV_DIAGNOSTICS
    lane.record = nullptr;
#endif
    return true;
}

// Install thread. Its own transaction: any miss leaves the game's own blips, said once.
bool InstallHalo3TrackerHudHeight(uintptr_t base, size_t size, uint32_t generation)
{
    namespace t = halo3_tracker_hud_height;
    auto& lane = g_halo3TrackerHudHeight;
    lane.ready.store(false, std::memory_order_release);
    if (lane.target)
    {
        LOG("TRACKER HUD HEIGHT Halo 3: StockFallback: a previous blip-draw hook has not retired");
        return false;
    }
    const auto unique = [&](const char* pattern) -> const uint8_t* {
        const uintptr_t hit = sig::Find(base, size, pattern);
        return hit && !sig::Find(hit + 1, base + size - hit - 1, pattern) ? reinterpret_cast<const uint8_t*>(hit)
                                                                           : nullptr;
    };
    const auto rva = [&](const uint8_t* at) {
        return at ? static_cast<int64_t>(reinterpret_cast<uintptr_t>(at) - base) : int64_t{0};
    };
    const uint8_t* const entry = unique(t::kDrawEntryPattern);
    const uint8_t* const call = unique(t::kDrawCallPattern);
    const uint8_t* const drawRead = unique(t::kDrawOriginPattern);
    const uint8_t* const objectRead = unique(t::kObjectOriginPattern);
    const uint8_t* const setupWrite = unique(t::kSetupWritePattern);
    t::Sites sites{};
    const t::Bind bind =
        t::DecodeSites(entry, rva(entry), call, rva(call), drawRead, rva(drawRead), objectRead, rva(objectRead),
                       setupWrite, rva(setupWrite), static_cast<int64_t>(size), sites);
    if (bind != t::Bind::Bound)
    {
        LOG("TRACKER HUD HEIGHT Halo 3: StockFallback: %s; the motion tracker's blips stay where the game draws them "
            "(below the tracker by the HUD height)", t::BindName(bind));
        return false;
    }
    float* const origin = reinterpret_cast<float*>(base + static_cast<uintptr_t>(sites.originRva));
    // The centre and its scale pair must be the title's own committed, writable data before anything is written.
    MEMORY_BASIC_INFORMATION page{};
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (VirtualQuery(origin, &page, sizeof(page)) != sizeof(page) || page.State != MEM_COMMIT ||
        (page.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(page.Protect & writable) ||
        page.AllocationBase != reinterpret_cast<void*>(base) ||
        reinterpret_cast<uintptr_t>(page.BaseAddress) + page.RegionSize < reinterpret_cast<uintptr_t>(origin) + 16)
    {
        LOG("TRACKER HUD HEIGHT Halo 3: StockFallback: the tracker's centre (+0x%llX) is not the title's own writable "
            "data; the motion tracker's blips stay where the game draws them",
            static_cast<unsigned long long>(sites.originRva));
        return false;
    }
    for (auto& use : lane.uses)
        use.store(0, std::memory_order_relaxed);
    lane.faults.store(0, std::memory_order_relaxed);
    lane.rewritten.store(0, std::memory_order_relaxed);
    lane.reportedState = -1;
    void* const target = reinterpret_cast<void*>(base + static_cast<uintptr_t>(sites.drawRva));
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Halo3TrackerBlipDrawDetour),
                      reinterpret_cast<void**>(&lane.original)) != MH_OK)
    {
        lane.original = nullptr;
        LOG("TRACKER HUD HEIGHT Halo 3: StockFallback: the blip-draw hook was not created");
        return false;
    }
    lane.target = target;
    lane.origin = origin;
#if HALOMCCVR_DEV_DIAGNOSTICS
    lane.record = reinterpret_cast<const uint8_t* const*>(base + static_cast<uintptr_t>(sites.recordRva));
#endif
    lane.ready.store(true, std::memory_order_release);
    if (MCCVR_EnableHook(target) != MH_OK)
    {
        LOG("TRACKER HUD HEIGHT Halo 3: StockFallback: the blip-draw hook did not enable");
        (void)RemoveHalo3TrackerHudHeight();
        return false;
    }
    LOG("TRACKER HUD HEIGHT Halo 3: Installed (generation %u): the blip draw +0x%llX places the motion tracker's blips "
        "around a centre it reads at +0x%llX, which the HUD frame setup copies from the current HUD-resolution record "
        "(+0x18) every pass and the HUD height hook never moves, while that hook raises the tracker's art by the "
        "HUD height (once, on its finished basis). For the length of each blip draw the centre's y gets "
        "that same move, then the game's own goes back. Proven: five unique signatures, the draw at its own entry "
        "and at its one call's operand, the centre at three sites (the draw's read, the contact builder's read, the "
        "frame setup's write) with the scale pair behind it at each, and the page is the title's own writable data. "
        "tracker_blips_follow_hud_height is %s",
        generation, static_cast<unsigned long long>(sites.drawRva), static_cast<unsigned long long>(sites.originRva),
        g_config.tracker_blips_follow_hud_height ? "on" : "off");
    lane.reportedSwitch = g_config.tracker_blips_follow_hud_height ? 1 : 0;
    return true;
}

// Cold worker, every 2 s. Every build: the player's switch when it changes, and the state when it changes (moved
// with the HUD height / the game's own, with the reason). Dev builds: the window.
void Halo3TrackerHudHeightReport()
{
    namespace t = halo3_tracker_hud_height;
    auto& lane = g_halo3TrackerHudHeight;
    const uint64_t now = GetTickCount64();
    if (now - lane.reportMs < 2000)
        return;
    lane.reportMs = now;
    if (!lane.ready.load(std::memory_order_acquire))
        return;
    const int switchNow = g_config.tracker_blips_follow_hud_height ? 1 : 0;
    if (lane.reportedSwitch != switchNow)
    {
        lane.reportedSwitch = switchNow;
        LOG("TRACKER HUD HEIGHT Halo 3: the switch is now %s: the motion tracker's blips %s", switchNow ? "ON" : "OFF",
            switchNow ? "are drawn on the tracker wherever the HUD height puts it"
                      : "are drawn where the game puts them, below the tracker by the HUD height");
    }
    uint32_t counts[static_cast<size_t>(t::Use::Count)]{};
    uint32_t total = 0, refused = 0;
    size_t most = static_cast<size_t>(t::Use::Count);
    for (size_t i = 0; i < static_cast<size_t>(t::Use::Count); ++i)
    {
        counts[i] = lane.uses[i].exchange(0, std::memory_order_relaxed);
        total += counts[i];
        if (i != static_cast<size_t>(t::Use::Lowered))
        {
            refused += counts[i];
            if (counts[i] && (most == static_cast<size_t>(t::Use::Count) || counts[i] > counts[most]))
                most = i;
        }
    }
    const uint32_t faults = lane.faults.exchange(0, std::memory_order_relaxed);
    const uint32_t rewritten = lane.rewritten.exchange(0, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    uint32_t chains[3];
    for (int i = 0; i < 3; ++i)
        chains[i] = g_halo3HudAnchorChains[i].exchange(0, std::memory_order_relaxed);
#endif
    if (!total && !faults && !rewritten)
        return;
    const uint32_t moved = counts[static_cast<size_t>(t::Use::Lowered)];
    const float height = lane.lastHeight.load(std::memory_order_relaxed);
    const float originY = lane.lastOriginY.load(std::memory_order_relaxed);
    const float loweredY = lane.lastLoweredY.load(std::memory_order_relaxed);
    // The state: 0 = moved with the HUD height; 1 + reason = the game's own for that reason (the larger share).
    const int state = moved >= refused ? 0 : 1 + static_cast<int>(most);
    if (state != lane.reportedState)
    {
        lane.reportedState = state;
        if (!state)
            LOG("TRACKER HUD HEIGHT Halo 3: the motion tracker's blips are drawn %.0f HUD units %s the game's own "
                "place, with the tracker's art (HUD height %+.0f)",
                std::fabs(loweredY - originY), loweredY < originY ? "above" : "below", height);
        else if (most != static_cast<size_t>(t::Use::SwitchOff))
            LOG("TRACKER HUD HEIGHT Halo 3: the motion tracker's blips are where the game draws them: %s",
                t::UseName(static_cast<t::Use>(most)));
    }
    if (rewritten)
        LOG("TRACKER HUD HEIGHT Halo 3: %u blip draws left the tracker's centre as they found it after the draw: it no "
            "longer held the value given to it (the game wrote it meanwhile)", rewritten);
#if HALOMCCVR_DEV_DIAGNOSTICS
    LOG("TRACKER HUD HEIGHT Halo 3 (dev): %u blip draws moved (last: HUD height %+.1f, the centre x %.2f y %.2f drawn at "
        "y %.2f; scale %.3f %.3f; the HUD record's own centre x %.2f y %.2f); not moved %u: switch off %u, the HUD "
        "height moved nothing %u, not a number %u, unreadable %u; faults %u, centre rewritten meanwhile %u; outermost "
        "anchor bases moved (each once): anchored directly %u, through one parent %u, nested deeper %u",
        moved, height, lane.lastOriginX.load(std::memory_order_relaxed), originY, loweredY,
        lane.lastScale[0].load(std::memory_order_relaxed), lane.lastScale[1].load(std::memory_order_relaxed),
        lane.lastRecord[0].load(std::memory_order_relaxed), lane.lastRecord[1].load(std::memory_order_relaxed),
        refused, counts[static_cast<size_t>(t::Use::SwitchOff)], counts[static_cast<size_t>(t::Use::NothingLowered)],
        counts[static_cast<size_t>(t::Use::NotFinite)], counts[static_cast<size_t>(t::Use::Unreadable)], faults,
        rewritten, chains[0], chains[1], chains[2]);
#endif
}
