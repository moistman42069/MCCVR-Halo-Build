// PROJECT RECLAIMER: ODST'S HUD DRAWS FROM THE CLIENT'S OWN COPY OF ODST'S LAYOUT RECORD (included by game.cpp, in its
// namespace, after the safe-frame helpers).
//
// Reclaimer 0.10 converts an ODST level's tags into the engine's, but draws their HUD as ODST's engine does, from its
// own parse of ODST's HUD globals: each of ODST's five layout records a 0x110-byte buffer, listed as {capacity,
// pointer, length}. Writing the HUD size to the engine's converted copy, to Halo 3's records or to the client's
// converted copy moved nothing; writing it to the 1280x720 buffer the list names shrank the HUD at once, and putting
// the title's value back there brought the full-size HUD back (FA, 2026-11-07). The heap also holds hundreds of freed
// buffers with the same bytes and two dozen lone entries naming them, and writing such copies closed the game, so only
// the buffers of the one list that names ODST's whole block are written, and the list is read again before every write.
//
// ALL FIVE OF THEM. Through 0.10.1 the client drew one player's HUD from the block's first record (1280x720). 0.10.2
// reworked ODST's split-screen HUD (each of up to four players gets the whole HUD), and on its first run the HUD was
// the game's size again with the list found and the 1280x720 buffer holding the written layout (2026-11-11): the
// client now draws from another of the block's records, plausibly the one nearest its screen's shape (the mod's
// 2912x2100 is nearer 920x690's than 1280x720's). Every record of that one list gets the layout, as Halo 3's records
// do, each keeping its own depth.
//
// AND 0.10.2'S OWN TABLE. That alone left the HUD wide: 0.10.2 keeps a table of the five records' leading fields of its
// own (reclaimer_odst_hud::TableAt), with the game's safe frames, two of them zero. The one such table is found in the
// same pass and its five pairs get the layout too, the table read whole and checked before every write.
//
// ON ITS OWN, FROM THE LEVEL'S START. The first build searched only after Halo 3's layout scan (6 s) and read all of
// memory twice (13 s): ODST's HUD kept the game's size for about 20 s of every level (2026-11-07 16:55). The search now
// starts as the level does, on a thread of its own, reads the client's memory once for the list alone, and the record
// is written whether or not Halo 3's records have been found yet.

#include "../common/reclaimer_odst_hud_logic.h"

// halo3_tag_names.inl: whether the loaded map's scenario is one of ODST's.
bool Halo3LoadedScenarioIsOdst();

struct ReclaimerOdstHudRecord
{
    std::atomic<uintptr_t> listEntry{0};
    std::atomic<uintptr_t> record{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint32_t> baseDepthBits[reclaimer_odst_hud::kBlockRecords]{};
    std::atomic<uintptr_t> table{0};   // 0.10.2's table (0: none found)
};
ReclaimerOdstHudRecord g_reclaimerOdstHud{};
// The depth field the last record found held when first found: the same buffer found again keeps it, so the curvature
// setting never compounds.
std::atomic<uintptr_t> g_reclaimerOdstHudRememberedRecord{0};
std::atomic<uint32_t> g_reclaimerOdstHudRememberedDepthBits[reclaimer_odst_hud::kBlockRecords]{};
// The search: one at a time, a few tries a level (the client's parse may not be listed yet at the very start).
std::atomic<bool> g_reclaimerOdstHudSearchInFlight{false};
std::atomic<uint32_t> g_reclaimerOdstHudSearchGeneration{0};
std::atomic<uint32_t> g_reclaimerOdstHudSearchTries{0};
std::atomic<uint64_t> g_reclaimerOdstHudSearchLastMs{0};
// The layout scan saw ODST's converted record in the engine's tag data in this generation.
std::atomic<uint32_t> g_reclaimerOdstHudMarkerGeneration{0};

static bool ReadOwnMemory(uintptr_t address, void* out, size_t size)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &got) &&
        got == size;
}

// Whether the five entries at `listEntry` list ODST's whole layout block.
static bool ReclaimerOdstHudListsBlock(uintptr_t listEntry)
{
    uint64_t words[3 * reclaimer_odst_hud::kBlockRecords]{};
    if (!listEntry || !ReadOwnMemory(listEntry, words, sizeof(words)))
        return false;
    reclaimer_odst_hud::RecordShape shapes[reclaimer_odst_hud::kBlockRecords]{};
    for (int i = 0; i < reclaimer_odst_hud::kBlockRecords; ++i)
    {
        if (!reclaimer_odst_hud::ListsRecord(words + 3 * i, words[3 * i + 1]) ||
            !ReadOwnMemory(static_cast<uintptr_t>(words[3 * i + 1]) + reclaimer_odst_hud::kWidthOffset, &shapes[i],
                           sizeof(shapes[i])))
            return false;
    }
    return reclaimer_odst_hud::ListsBlock(words, shapes);
}

// Still the first of ODST's block in its list, and still ODST's 1280x720 record.
static bool ReclaimerOdstHudRecordHolds(uintptr_t listEntry, uintptr_t record)
{
    uint64_t words[3]{};
    if (!listEntry || !ReadOwnMemory(listEntry, words, sizeof(words)) ||
        !reclaimer_odst_hud::ListsRecord(words, record) || !ReclaimerOdstHudListsBlock(listEntry))
        return false;
    const HudLayoutAnchorView view = HudLayoutAnchorAt(kOdstHudLayoutAdapter, 0);
    unsigned char anchor[kHudLayoutMaxAnchor]{};
    return ReadOwnMemory(record + reclaimer_odst_hud::kWidthOffset, anchor, static_cast<size_t>(view.anchorLength)) &&
        memcmp(anchor, view.anchor, 8) == 0 && SafeFrameAnchorEquals(anchor, view);
}

// The five records the list at `listEntry` names, in the block's order.
static bool ReclaimerOdstHudBlockRecords(uintptr_t listEntry, uintptr_t (&records)[reclaimer_odst_hud::kBlockRecords])
{
    uint64_t words[3 * reclaimer_odst_hud::kBlockRecords]{};
    if (!listEntry || !ReadOwnMemory(listEntry, words, sizeof(words)))
        return false;
    for (int i = 0; i < reclaimer_odst_hud::kBlockRecords; ++i)
        records[i] = static_cast<uintptr_t>(words[3 * i + 1]);
    return true;
}

// Where a record's safe-frame pair sits.
static uintptr_t ReclaimerOdstHudSlot(uintptr_t record)
{
    return record + reclaimer_odst_hud::kWidthOffset + static_cast<uintptr_t>(kOdstHudLayoutAdapter.safeFrameOffset);
}

// Calls `visit(base, size)` for each committed private read-write region outside this module. False: the level
// changed on the way.
template <class Visit>
static bool ForEachPrivateReadWriteRegion(uint32_t generation, Visit&& visit)
{
    uintptr_t selfBase = 0;
    size_t selfSize = 0;
    sig::SelfModuleRange(selfBase, selfSize);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t end = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < end && VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        if (!HudLayoutContextMatches(HudLayoutProfile::Halo3, generation))
            return false;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const bool self = selfBase && base >= selfBase && base < selfBase + selfSize;
        if (!self && mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE)
            visit(base, static_cast<size_t>(mbi.RegionSize));
        addr = base + mbi.RegionSize;
    }
    return true;
}

// One pass over the client's memory for a list of ODST's whole block: five {capacity, pointer, 0x110} entries in a
// row, each checked against the record it names. The engine's tag data (16 GiB from its base) is the game's, not the
// client's parse, and is skipped.
static void FindReclaimerOdstHudRecord(uint32_t generation, int attempt)
{
    const uint64_t t0 = GetTickCount64();
    constexpr uintptr_t kTagDataSpan = uintptr_t{1} << 34;
    const uintptr_t tagBase = Halo3TagDataBaseValue();
    constexpr size_t kEntryWords = 3 * reclaimer_odst_hud::kBlockRecords;
    constexpr size_t kWindowWords = (size_t{1} << 17) + kEntryWords;
    static uint64_t s_window[kWindowWords];
    uintptr_t listEntry = 0, record = 0, table = 0;
    int listed = 0, tabled = 0;
    uint64_t scanned = 0;
    // Windows overlap by a whole table, so one across a window's end is seen in the next; anything seen twice there is
    // counted once (addresses only rise).
    constexpr size_t kOverlap = (std::max)((kEntryWords - 1) * 8, (reclaimer_odst_hud::kTableBytes + 7) / 8 * 8);
    if (!ForEachPrivateReadWriteRegion(generation, [&](uintptr_t base, size_t size) {
            if (tagBase && base >= tagBase && base - tagBase < kTagDataSpan)
                return;
            scanned += size;
            size_t pos = 0;
            while (pos + kEntryWords * 8 <= size)
            {
                if (!HudLayoutContextMatches(HudLayoutProfile::Halo3, generation))
                    return;
                const size_t want = (std::min)(size - pos, kWindowWords * 8);
                SIZE_T got = 0;
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(base + pos), s_window, want, &got);
                if (got < kEntryWords * 8)
                {
                    pos = ((pos >> 12) + 1) << 12;
                    continue;
                }
                const size_t words = got / 8;
                for (size_t w = 0; w + kEntryWords <= words; ++w)
                {
                    if (s_window[w + 2] != reclaimer_odst_hud::kRecordLength ||
                        s_window[w + 5] != reclaimer_odst_hud::kRecordLength ||
                        s_window[w + 8] != reclaimer_odst_hud::kRecordLength ||
                        s_window[w + 11] != reclaimer_odst_hud::kRecordLength ||
                        s_window[w + 14] != reclaimer_odst_hud::kRecordLength)
                        continue;
                    const uintptr_t entry = base + pos + w * 8;
                    if (entry <= listEntry ||
                        !ReclaimerOdstHudRecordHolds(entry, static_cast<uintptr_t>(s_window[w + 1])))
                        continue;
                    ++listed;
                    listEntry = entry;
                    record = static_cast<uintptr_t>(s_window[w + 1]);
                }
                const auto* bytes = reinterpret_cast<const uint8_t*>(s_window);
                for (size_t b = 0; b + reclaimer_odst_hud::kTableBytes <= got; b += 4)
                {
                    uint32_t first[2]{};
                    memcpy(first, bytes + b, sizeof(first));
                    if (first[0] != reclaimer_odst_hud::kBlockShapes[0].width ||
                        first[1] != reclaimer_odst_hud::kBlockShapes[0].height || base + pos + b <= table ||
                        !reclaimer_odst_hud::TableAt(bytes + b))
                        continue;
                    ++tabled;
                    table = base + pos + b;
                }
                // The next window starts where an entry or a table not yet seen whole could begin.
                pos += got == want && pos + got < size ? got - kOverlap : (((pos + got) >> 12) + 1) << 12;
            }
        }))
        return;

    // Only what this search found: a table from an earlier level may be freed memory now.
    g_reclaimerOdstHud.table.store(tabled == 1 ? table : 0, std::memory_order_relaxed);
    if (tabled == 1)
    {
        LOG("SAFEFRAME [Halo 3]: ODST's level on Project Reclaimer: the client's own table of ODST's five layouts at %p "
            "gets the layout too (Project Reclaimer 0.10.2 and later)", reinterpret_cast<void*>(table));
    }
    else if (tabled)
        LOG("SAFEFRAME [Halo 3]: ODST's level on Project Reclaimer, try %d: %d tables of ODST's five layouts, not told "
            "apart; nothing written to them", attempt, tabled);
    if (listed != 1)
    {
        LOG("SAFEFRAME [Halo 3]: ODST's level on Project Reclaimer, try %d: %d list(s) name ODST's whole layout block "
            "in %llu MB of the client's memory; %s (%llu ms)", attempt, listed,
            static_cast<unsigned long long>(scanned >> 20),
            listed ? "not told apart, nothing written there" : "ODST's HUD keeps the game's size for now",
            static_cast<unsigned long long>(GetTickCount64() - t0));
        if (tabled == 1 && HudLayoutContextMatches(HudLayoutProfile::Halo3, generation))
            g_reclaimerOdstHud.generation.store(generation, std::memory_order_release);
        return;
    }
    uintptr_t records[reclaimer_odst_hud::kBlockRecords]{};
    uint32_t depths[reclaimer_odst_hud::kBlockRecords]{};
    if (!ReclaimerOdstHudBlockRecords(listEntry, records))
        return;
    for (int i = 0; i < reclaimer_odst_hud::kBlockRecords; ++i)
        if (!ReadOwnMemory(ReclaimerOdstHudSlot(records[i]) + kOdstHudLayoutAdapter.depthFromSlot, &depths[i],
                           sizeof(depths[i])) || !HudDestinationPlausible(depths[i]))
            return;
    if (!HudLayoutContextMatches(HudLayoutProfile::Halo3, generation))
        return;
    const bool remembered = g_reclaimerOdstHudRememberedRecord.exchange(record, std::memory_order_relaxed) == record;
    for (int i = 0; i < reclaimer_odst_hud::kBlockRecords; ++i)
    {
        if (remembered)
            depths[i] = g_reclaimerOdstHudRememberedDepthBits[i].load(std::memory_order_relaxed);
        else
            g_reclaimerOdstHudRememberedDepthBits[i].store(depths[i], std::memory_order_relaxed);
        g_reclaimerOdstHud.baseDepthBits[i].store(depths[i], std::memory_order_relaxed);
    }
    g_reclaimerOdstHud.listEntry.store(listEntry, std::memory_order_relaxed);
    g_reclaimerOdstHud.record.store(record, std::memory_order_relaxed);
    g_reclaimerOdstHud.generation.store(generation, std::memory_order_release);
    LOG("SAFEFRAME [Halo 3]: ODST's level on Project Reclaimer: the client draws its HUD from its own copy of ODST's "
        "layout records, the first at %p (ODST's block listed at %p; try %d, %llu MB of the client's memory read); the "
        "layout is written to all five (%llu ms)", reinterpret_cast<void*>(record), reinterpret_cast<void*>(listEntry),
        attempt, static_cast<unsigned long long>(scanned >> 20),
        static_cast<unsigned long long>(GetTickCount64() - t0));
}

static DWORD WINAPI ReclaimerOdstHudSearchThread(LPVOID parameter)
{
    const uintptr_t token = reinterpret_cast<uintptr_t>(parameter);
    FindReclaimerOdstHudRecord(static_cast<uint32_t>(token), static_cast<int>(token >> 32));
    g_reclaimerOdstHudSearchInFlight.store(false, std::memory_order_release);
    return 0;
}

// 0.10.2's table: its five safe-frame pairs get the layout when they differ, the table read whole and checked first.
// False: there is none, or it is no longer the client's.
static bool ApplyReclaimerOdstHudTable(uint32_t generation, float horizontal, float vertical, bool reportReadback)
{
    const uintptr_t table = g_reclaimerOdstHud.table.load(std::memory_order_relaxed);
    if (!table)
        return false;
    uint8_t bytes[reclaimer_odst_hud::kTableBytes]{};
    if (!ReadOwnMemory(table, bytes, sizeof(bytes)) || !reclaimer_odst_hud::TableAt(bytes))
    {
        g_reclaimerOdstHud.table.store(0, std::memory_order_relaxed);
        LOG("SAFEFRAME [Halo 3]: Project Reclaimer's table of ODST's layouts at %p is gone; not written again",
            reinterpret_cast<void*>(table));
        return false;
    }
    const float pair[2]{horizontal, vertical};
    int holding = 0;
    for (int i = 0; i < reclaimer_odst_hud::kBlockRecords; ++i)
    {
        const size_t at = static_cast<size_t>(i) * reclaimer_odst_hud::kTableStride + reclaimer_odst_hud::kTablePairOffset;
        if (memcmp(bytes + at, pair, sizeof(pair)) == 0)
        {
            ++holding;
            continue;
        }
        SIZE_T wrote = 0;
        AcquireSRWLockShared(&g_hudLayoutWriteLock);
        if (HudLayoutContextMatches(HudLayoutProfile::Halo3, generation))
            WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(table + at), pair, sizeof(pair), &wrote);
        ReleaseSRWLockShared(&g_hudLayoutWriteLock);
    }
    if (reportReadback)
        LOG("SAFEFRAME [Halo 3]: Project Reclaimer's table of ODST's layouts: %d of %d already held %.4f/%.4f",
            holding, reclaimer_odst_hud::kBlockRecords, horizontal, vertical);
    return true;
}

// Writes the layout to the record when it differs. False: the record is no longer the client's.
static bool ApplyReclaimerOdstHudRecord(uint32_t generation, float horizontal, float vertical, float curvature,
                                        bool reportReadback)
{
    const bool tabled = ApplyReclaimerOdstHudTable(generation, horizontal, vertical, reportReadback);
    const uintptr_t record = g_reclaimerOdstHud.record.load(std::memory_order_relaxed);
    if (!record)
        return tabled;
    const uintptr_t listEntry = g_reclaimerOdstHud.listEntry.load(std::memory_order_relaxed);
    if (!ReclaimerOdstHudRecordHolds(listEntry, record))
    {
        g_reclaimerOdstHud.record.store(0, std::memory_order_relaxed);
        LOG("SAFEFRAME [Halo 3]: Project Reclaimer no longer lists ODST's layout record at %p; not written again",
            reinterpret_cast<void*>(record));
        return tabled;
    }
    uintptr_t records[reclaimer_odst_hud::kBlockRecords]{};
    if (!ReclaimerOdstHudBlockRecords(listEntry, records))
        return true;
    int holding = 0;
    for (int i = 0; i < reclaimer_odst_hud::kBlockRecords; ++i)
    {
        const uintptr_t slot = ReclaimerOdstHudSlot(records[i]);
        float pair[2]{};
        uint32_t depthBits = 0;
        if (!ReadOwnMemory(slot, pair, sizeof(pair)) ||
            !ReadOwnMemory(slot + kOdstHudLayoutAdapter.depthFromSlot, &depthBits, sizeof(depthBits)))
            continue;
        float baseDepth = 0.0f;
        const uint32_t baseBits = g_reclaimerOdstHud.baseDepthBits[i].load(std::memory_order_relaxed);
        memcpy(&baseDepth, &baseBits, sizeof(baseDepth));
        // As the records ApplyHudLayoutOnce writes: 0 flat, 1 fully curved, 0.5 the authored value.
        const float depth = baseDepth + (0.30f - 0.60f * curvature);
        uint32_t targetBits = 0;
        memcpy(&targetBits, &depth, sizeof(targetBits));
        if (!HudDestinationPlausible(targetBits))
            continue;
        if (reportReadback && i == 0)
            LOG("SAFEFRAME [Halo 3]: Project Reclaimer's ODST record at %p reads %.4f/%.4f, writing %.4f/%.4f to "
                "all five", reinterpret_cast<void*>(records[i]), pair[0], pair[1], horizontal, vertical);
        if (pair[0] == horizontal && pair[1] == vertical && depthBits == targetBits)
        {
            ++holding;
            continue;
        }
        AcquireSRWLockShared(&g_hudLayoutWriteLock);
        if (HudLayoutContextMatches(HudLayoutProfile::Halo3, generation))
            SafeFrameStoreLayout(slot, kOdstHudLayoutAdapter, depth, horizontal, vertical);
        ReleaseSRWLockShared(&g_hudLayoutWriteLock);
    }
    if (reportReadback)
        LOG("SAFEFRAME [Halo 3]: Project Reclaimer's ODST records: %d of %d already held the layout", holding,
            reclaimer_odst_hud::kBlockRecords);
    return true;
}

// Whether the running level is ODST's (Game_ReclaimerOdstLevel, end of game.cpp), as the HUD tick below last judged it.
std::atomic<bool> g_reclaimerOdstLevel{false};

// Present thread, from HudLayoutAutoTick while a Halo 3 level runs on Project Reclaimer: starts the search on an ODST
// level, then keeps the record holding the layout the player's settings and the Aim Frame want.
static void ReclaimerOdstHudTick(uint32_t generation, uint64_t now, float horizontal, float vertical,
                                 float curvature)
{
    const bool found = (g_reclaimerOdstHud.record.load(std::memory_order_relaxed) ||
                        g_reclaimerOdstHud.table.load(std::memory_order_relaxed)) &&
        g_reclaimerOdstHud.generation.load(std::memory_order_acquire) == generation;
    g_reclaimerOdstLevel.store(
        found ||
            g_reclaimerOdstHudMarkerGeneration.load(std::memory_order_relaxed) == generation ||
            Halo3LoadedScenarioIsOdst(),
        std::memory_order_relaxed);
    if (found)
    {
        static uint64_t s_lastApplyMs = 0, s_lastReadbackMs = 0;
        static float s_lastH = 0.0f, s_lastV = 0.0f, s_lastCurvature = -1.0f;
        const bool changed = horizontal != s_lastH || vertical != s_lastV || curvature != s_lastCurvature;
        if (!changed && now - s_lastApplyMs < 1000)
            return;
        s_lastApplyMs = now;
        s_lastH = horizontal;
        s_lastV = vertical;
        s_lastCurvature = curvature;
        const bool readback = now - s_lastReadbackMs >= 5000;
        if (readback)
            s_lastReadbackMs = now;
        ApplyReclaimerOdstHudRecord(generation, horizontal, vertical, curvature, readback);
        return;
    }

    if (!g_activeTitleLevelRunning.load(std::memory_order_acquire) ||
        g_reclaimerOdstHudSearchInFlight.load(std::memory_order_acquire))
        return;
    const bool odstLevel = g_reclaimerOdstHudMarkerGeneration.load(std::memory_order_relaxed) == generation ||
        Halo3LoadedScenarioIsOdst();
    if (!odstLevel)
        return;
    constexpr uint32_t kTries = 4;
    constexpr uint64_t kRetryMs = 3000;
    if (g_reclaimerOdstHudSearchGeneration.load(std::memory_order_relaxed) != generation)
    {
        g_reclaimerOdstHudSearchGeneration.store(generation, std::memory_order_relaxed);
        g_reclaimerOdstHudSearchTries.store(0, std::memory_order_relaxed);
    }
    else if (g_reclaimerOdstHudSearchTries.load(std::memory_order_relaxed) >= kTries ||
             now - g_reclaimerOdstHudSearchLastMs.load(std::memory_order_relaxed) < kRetryMs)
        return;
    if (g_reclaimerOdstHudSearchInFlight.exchange(true, std::memory_order_acq_rel))
        return;
    const uint32_t attempt = g_reclaimerOdstHudSearchTries.fetch_add(1, std::memory_order_relaxed) + 1;
    g_reclaimerOdstHudSearchLastMs.store(now, std::memory_order_relaxed);
    const uintptr_t token = static_cast<uintptr_t>(generation) | (static_cast<uintptr_t>(attempt) << 32);
    HANDLE thread = CreateThread(nullptr, 0, ReclaimerOdstHudSearchThread, reinterpret_cast<LPVOID>(token), 0, nullptr);
    if (thread)
        CloseHandle(thread);
    else
        g_reclaimerOdstHudSearchInFlight.store(false, std::memory_order_release);
}
