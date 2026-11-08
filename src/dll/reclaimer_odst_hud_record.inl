// PROJECT RECLAIMER: ODST'S HUD DRAWS FROM THE CLIENT'S OWN COPY OF ODST'S LAYOUT RECORD (included by game.cpp, in its
// namespace, after the safe-frame helpers).
//
// Reclaimer 0.10 converts an ODST level's tags into the engine's, but draws their HUD as ODST's engine does, from its
// own parse of ODST's HUD globals: each of ODST's five layout records a 0x110-byte buffer, listed as {capacity,
// pointer, length}. Writing the HUD size to the engine's converted copy, to Halo 3's records or to the client's
// converted copy moved nothing; writing it to the 1280x720 buffer the list names shrank the HUD at once, and putting
// the title's value back there brought the full-size HUD back (FA, 2026-11-07). The heap also holds hundreds of freed
// buffers with the same bytes and two dozen lone entries naming them, and writing such copies closed the game, so only
// the 1280x720 buffer of the one list that names ODST's whole block is written, and the list is read again before
// every write.

#include "../common/reclaimer_odst_hud_logic.h"

struct ReclaimerOdstHudRecord
{
    std::atomic<uintptr_t> listEntry{0};
    std::atomic<uintptr_t> record{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint32_t> baseDepthBits{0};
};
ReclaimerOdstHudRecord g_reclaimerOdstHud{};
// The depth field the last record found held when first found: the same buffer found again keeps it, so the curvature
// setting never compounds.
std::atomic<uintptr_t> g_reclaimerOdstHudRememberedRecord{0};
std::atomic<uint32_t> g_reclaimerOdstHudRememberedDepthBits{0};

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

// Scan thread, after an ODST level's layout records are published. Two passes over the process: every buffer holding
// ODST's 1280x720 record, then every list entry that names one of them and goes on to list the rest of ODST's block.
static void FindReclaimerOdstHudRecord(uint32_t generation)
{
    constexpr int kMaxCandidates = 4096;
    static uintptr_t s_slots[kMaxCandidates];
    static uintptr_t s_candidates[kMaxCandidates];
    const uint64_t t0 = GetTickCount64();
    int slots = 0;
    if (!ForEachPrivateReadWriteRegion(generation, [&](uintptr_t base, size_t size) {
            if (slots < kMaxCandidates)
                slots += SafeFrameScanRegion(base, size, kOdstHudLayoutAdapter, s_slots + slots,
                                             kMaxCandidates - slots);
        }))
        return;
    const uintptr_t slotToRecord = static_cast<uintptr_t>(kOdstHudLayoutAdapter.safeFrameOffset) +
        reclaimer_odst_hud::kWidthOffset;
    for (int i = 0; i < slots; ++i)
        s_candidates[i] = s_slots[i] - slotToRecord;
    std::sort(s_candidates, s_candidates + slots);

    constexpr size_t kWindowWords = (size_t{1} << 17) + 2;
    static uint64_t s_window[kWindowWords];
    uintptr_t listEntry = 0, record = 0;
    int listed = 0, lone = 0;
    if (!ForEachPrivateReadWriteRegion(generation, [&](uintptr_t base, size_t size) {
            if (!slots)
                return;
            size_t pos = 0;
            while (pos + 24 <= size)
            {
                const size_t want = (std::min)(size - pos, kWindowWords * 8);
                SIZE_T got = 0;
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(base + pos), s_window, want, &got);
                if (got < 24)
                {
                    pos = ((pos >> 12) + 1) << 12;
                    continue;
                }
                const size_t words = got / 8;
                for (size_t w = 0; w + 2 < words; ++w)
                {
                    if (s_window[w + 2] != reclaimer_odst_hud::kRecordLength ||
                        !reclaimer_odst_hud::IsCandidate(s_candidates, static_cast<size_t>(slots), s_window[w + 1]) ||
                        !reclaimer_odst_hud::ListsRecord(s_window + w, s_window[w + 1]))
                        continue;
                    if (!ReclaimerOdstHudListsBlock(base + pos + w * 8))
                    {
                        ++lone;
                        continue;
                    }
                    ++listed;
                    listEntry = base + pos + w * 8;
                    record = static_cast<uintptr_t>(s_window[w + 1]);
                }
                // Overlap two words so an entry across the window's edge is seen whole.
                pos += got == want && pos + got < size ? got - 16 : (((pos + got) >> 12) + 1) << 12;
            }
        }))
        return;

    if (listed != 1)
    {
        LOG("SAFEFRAME [Halo 3]: ODST's level on Project Reclaimer: %d buffer(s) hold ODST's layout record, %d list(s) "
            "name ODST's whole block and %d lone entr%s name one; %s (%llu ms)", slots, listed, lone,
            lone == 1 ? "y" : "ies", listed ? "not told apart, nothing written there" : "ODST's HUD keeps the game's size",
            static_cast<unsigned long long>(GetTickCount64() - t0));
        return;
    }
    const uintptr_t slot = record + slotToRecord;
    uint32_t depth = 0;
    if (!ReadOwnMemory(slot + kOdstHudLayoutAdapter.depthFromSlot, &depth, sizeof(depth)) ||
        !HudDestinationPlausible(depth) || !HudLayoutContextMatches(HudLayoutProfile::Halo3, generation))
        return;
    if (g_reclaimerOdstHudRememberedRecord.exchange(record, std::memory_order_relaxed) == record)
        depth = g_reclaimerOdstHudRememberedDepthBits.load(std::memory_order_relaxed);
    else
        g_reclaimerOdstHudRememberedDepthBits.store(depth, std::memory_order_relaxed);
    g_reclaimerOdstHud.listEntry.store(listEntry, std::memory_order_relaxed);
    g_reclaimerOdstHud.record.store(record, std::memory_order_relaxed);
    g_reclaimerOdstHud.baseDepthBits.store(depth, std::memory_order_relaxed);
    g_reclaimerOdstHud.generation.store(generation, std::memory_order_release);
    LOG("SAFEFRAME [Halo 3]: ODST's level on Project Reclaimer: the client draws its HUD from its own copy of ODST's "
        "layout record at %p (first of ODST's block listed at %p; %d buffer(s) hold the record and %d lone entr%s name "
        "one, the others freed); the layout is written there too (%llu ms)", reinterpret_cast<void*>(record),
        reinterpret_cast<void*>(listEntry), slots, lone, lone == 1 ? "y" : "ies",
        static_cast<unsigned long long>(GetTickCount64() - t0));
}

// Present thread, from ApplyHudLayoutOnce with the layout its records get.
static void ApplyReclaimerOdstHudRecord(uint32_t generation, float horizontal, float vertical, float curvature,
                                        bool reportReadback)
{
    const uintptr_t record = g_reclaimerOdstHud.record.load(std::memory_order_relaxed);
    if (!record || g_reclaimerOdstHud.generation.load(std::memory_order_acquire) != generation)
        return;
    const uintptr_t listEntry = g_reclaimerOdstHud.listEntry.load(std::memory_order_relaxed);
    if (!ReclaimerOdstHudRecordHolds(listEntry, record))
    {
        g_reclaimerOdstHud.record.store(0, std::memory_order_relaxed);
        LOG("SAFEFRAME [Halo 3]: Project Reclaimer no longer lists ODST's layout record at %p; not written again",
            reinterpret_cast<void*>(record));
        return;
    }
    const uintptr_t slot = record + reclaimer_odst_hud::kWidthOffset +
        static_cast<uintptr_t>(kOdstHudLayoutAdapter.safeFrameOffset);
    float pair[2]{};
    uint32_t depthBits = 0;
    if (!ReadOwnMemory(slot, pair, sizeof(pair)) ||
        !ReadOwnMemory(slot + kOdstHudLayoutAdapter.depthFromSlot, &depthBits, sizeof(depthBits)))
        return;
    float baseDepth = 0.0f;
    const uint32_t baseBits = g_reclaimerOdstHud.baseDepthBits.load(std::memory_order_relaxed);
    memcpy(&baseDepth, &baseBits, sizeof(baseDepth));
    // As the records ApplyHudLayoutOnce writes: 0 flat, 1 fully curved, 0.5 the authored value.
    const float depth = baseDepth + (0.30f - 0.60f * curvature);
    uint32_t targetBits = 0;
    memcpy(&targetBits, &depth, sizeof(targetBits));
    if (!HudDestinationPlausible(targetBits))
        return;
    if (reportReadback)
        LOG("SAFEFRAME [Halo 3]: Project Reclaimer's ODST record at %p reads %.4f/%.4f, writing %.4f/%.4f",
            reinterpret_cast<void*>(record), pair[0], pair[1], horizontal, vertical);
    if (pair[0] == horizontal && pair[1] == vertical && depthBits == targetBits)
        return;
    AcquireSRWLockShared(&g_hudLayoutWriteLock);
    if (HudLayoutContextMatches(HudLayoutProfile::Halo3, generation) &&
        HudLayoutResultsMatch(HudLayoutProfile::Halo3, generation))
        SafeFrameStoreLayout(slot, kOdstHudLayoutAdapter, depth, horizontal, vertical);
    ReleaseSRWLockShared(&g_hudLayoutWriteLock);
}
