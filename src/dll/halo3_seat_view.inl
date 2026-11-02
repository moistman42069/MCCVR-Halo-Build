// THE HALO 3 SEAT VIEW: FIRST PERSON FOR THE CAMERA DIRECTOR ONLY. Included by game.cpp, after
// seat_aim_world_frame.inl. The why, the addresses and the relay are in src/common/seat_view_logic.h.
//
// WHAT THIS DOES TO THE GAME: three `call rel32` instructions in the camera director (halo3+0x212074, +0x2127EF,
// +0x212DCB) are pointed at a relay in memory of the mod's own, next to the module. The relay calls the function
// those calls went to, unchanged, and for one unit -- the local player, seated, while the mod's own seat camera
// owns the view -- answers "first person" where the game answered a settled "third person"; and, the other way
// round, for a seat that is first person in its own data while a chase camera of the mod's shows it, "third
// person" where the game answered a settled "first person" (seat_view_logic.h, THE OTHER WAY ROUND). No tag, no object
// field and no instruction on any path the simulation runs is written; the function itself is not hooked, so a
// host that hooks it for its own camera modes keeps its hook and the relay calls through it.
//
// THREADS. Install, sync and remove run on the WaitThread and own g_halo3SeatView. A site's five bytes are
// swapped with every other thread suspended and none of them inside the bytes (the freeze seat_aim_world_frame.inl
// uses). The relay runs on whichever thread the director runs on; it reads two atomics (the armed unit and the
// chase unit), which the camera thread writes from the seat sampler (halo3_native_seat_patch.inl).
//
// LIFETIME. The relay holds the module's address, so it is built per module generation and retired with it. Two
// rules hold on every path, teardown and a changed generation alike:
//   * a site that still holds this relay's redirect is put back before anything is released or forgotten. Such a
//     site is in the mapping it was patched in, whatever the generation says; a site whose bytes are not ours (the
//     module is gone, or someone else wrote there) is left alone;
//   * the relay's memory is released only when no site calls it, its own count of threads inside it is zero, and no
//     suspended thread's instruction pointer lies in it.
// Removal reports "not yet" and is retried, like the other Halo 3 teardown steps. If it cannot finish in its time
// the teardown goes on, and the state is KEPT, marked retired: the relay then answers the game's own answer (no
// unit is armed), and every poll goes on putting the sites back and releases the relay when it can.

namespace
{
struct Halo3SeatViewSite
{
    uint8_t* call = nullptr;
    uint8_t stock[seat_view::kCallBytes]{};
    uint8_t redirect[seat_view::kCallBytes]{};
    bool patched = false;
};
struct Halo3SeatViewState
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    uint8_t* relay = nullptr;            // one allocation: the relay, then its unwind data
    RUNTIME_FUNCTION* unwind = nullptr;  // inside that allocation
    bool unwindRegistered = false;
    Halo3SeatViewSite sites[seat_view::kDirectorSiteCount];
    bool standDown = false;              // a site turned out not to be ours: nothing more is redirected here
    bool retired = false;                // torn down but not yet released: sites are only ever put back
    bool pendingLogged = false;
    uint32_t removeFailures = 0;
};
// An install that had to wait for another generation's relay to be retired: taken up again by the poll.
struct Halo3SeatViewRetry
{
    uintptr_t base = 0;
    size_t size = 0;
    uint32_t generation = 0;
};
constexpr size_t kHalo3SeatViewRelayAllocation = 0x1000;
constexpr size_t kHalo3SeatViewUnwindInfoOffset = 0x100;
constexpr size_t kHalo3SeatViewUnwindEntryOffset = 0x140;

Halo3SeatViewState g_halo3SeatView;                     // WaitThread only
Halo3SeatViewRetry g_halo3SeatViewRetry;                // WaitThread only
std::atomic<bool> g_halo3SeatViewRedirected{false};     // every director site calls the relay
std::atomic<int32_t> g_halo3SeatViewUnit{-1};           // the occupant the director is told is first person
std::atomic<int32_t> g_halo3SeatViewChaseUnit{-1};      // ...and the one it is told is third person (a chase camera)
std::atomic<uint32_t> g_halo3SeatViewChaseAnswered{0};  // how often that changed the game's answer (the log's witness)
volatile LONG g_halo3SeatViewInside = 0;                // threads inside the relay or below it (the relay counts)
#if HALOMCCVR_DEV_DIAGNOSTICS
std::atomic<uint32_t> g_halo3SeatViewAsked{0}, g_halo3SeatViewAnswered{0};   // the dev SEAT VIEW line
#endif

// The relay's second call, on the director's thread. Reads one atomic and the director's own local.
int Halo3SeatViewAdjust(int unit, const int* state, int result) noexcept
{
    Halo3HookCalled(Halo3Hook::SeatView);
#if HALOMCCVR_DEV_DIAGNOSTICS
    g_halo3SeatViewAsked.fetch_add(1, std::memory_order_relaxed);
#endif
    if ((result != seat_view::kThirdPerson && result != seat_view::kFirstPerson) || !state)
        return result;
    const int answer = seat_view::Decide(result, *state, unit, g_halo3SeatViewUnit.load(std::memory_order_acquire),
                                         g_halo3SeatViewChaseUnit.load(std::memory_order_acquire));
    if (answer == seat_view::kThirdPerson && result == seat_view::kFirstPerson)
        g_halo3SeatViewChaseAnswered.fetch_add(1, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    if (answer != result)
        g_halo3SeatViewAnswered.fetch_add(1, std::memory_order_relaxed);
#endif
    return answer;
}

bool Halo3SeatViewRead(const uint8_t* at, uint8_t out[seat_view::kCallBytes])
{
    __try
    {
        memcpy(out, at, seat_view::kCallBytes);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
bool Halo3SeatViewWrite(uint8_t* at, const uint8_t* bytes)
{
    __try
    {
        memcpy(at, bytes, seat_view::kCallBytes);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Memory within a rel32's reach of `target`: the first free region found walking up from 0x70000000 below it.
uint8_t* Halo3SeatViewAllocateNear(uintptr_t target, size_t bytes)
{
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const uintptr_t granularity = system.dwAllocationGranularity;
    if (!granularity || (granularity & (granularity - 1)) != 0)
        return nullptr;
    constexpr uintptr_t reach = 0x70000000u;
    const uintptr_t minimum = target > reach + granularity ? target - reach : granularity;
    const uintptr_t maximum = target <= UINTPTR_MAX - reach ? target + reach : UINTPTR_MAX;
    uintptr_t cursor = (minimum + granularity - 1) & ~(granularity - 1);
    for (uint32_t query = 0; cursor < maximum && query < 0x20000; ++query)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info))
            break;
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(info.BaseAddress);
        const uintptr_t regionEnd = regionBase <= UINTPTR_MAX - info.RegionSize ? regionBase + info.RegionSize
                                                                                : UINTPTR_MAX;
        if (info.State == MEM_FREE)
        {
            const uintptr_t candidate = ((regionBase < cursor ? cursor : regionBase) + granularity - 1) &
                ~(granularity - 1);
            if (candidate < maximum && bytes <= maximum - candidate && candidate < regionEnd &&
                bytes <= regionEnd - candidate)
            {
                void* const allocation = VirtualAlloc(reinterpret_cast<void*>(candidate), bytes,
                                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (allocation)
                    return static_cast<uint8_t*>(allocation);
            }
        }
        if (regionEnd <= cursor)
            break;
        cursor = regionEnd;
    }
    return nullptr;
}

enum class Halo3SeatViewSwap
{
    Done,
    Busy,      // no thread freeze, or a thread inside the bytes: try again on the next poll
    Foreign,   // the bytes are neither the expected ones nor the replacement: not ours any more
};

// Replace `expected` with `replacement` at a call site while every other thread is frozen and outside the bytes.
Halo3SeatViewSwap Halo3SeatViewSwapBytes(uint8_t* at, const uint8_t* expected, const uint8_t* replacement)
{
    constexpr size_t n = seat_view::kCallBytes;
    uint8_t now[n];
    if (!SeatAimFrameExecutable(at, n) || !Halo3SeatViewRead(at, now))
        return Halo3SeatViewSwap::Foreign;
    if (!memcmp(now, replacement, n))
        return Halo3SeatViewSwap::Done;
    if (memcmp(now, expected, n))
        return Halo3SeatViewSwap::Foreign;
    DWORD previous = 0;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &previous))
        return Halo3SeatViewSwap::Busy;
    Halo3SeatViewSwap result = Halo3SeatViewSwap::Busy;
    {
        ReachThreadFreeze frozen;
        if (frozen.Capture())
        {
            bool safe = true;
            const auto site = reinterpret_cast<DWORD64>(at);
            for (const ReachFrozenThread& thread : frozen.Threads())
            {
                if (!thread.suspended)
                    continue;
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (!GetThreadContext(thread.handle, &context))
                {
                    if (WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                        safe = false;
                    continue;
                }
                if (context.Rip > site && context.Rip < site + n)
                    safe = false;
            }
            if (safe)
            {
                uint8_t check[n];
                if (!Halo3SeatViewRead(at, check) || memcmp(check, expected, n))
                    result = Halo3SeatViewSwap::Foreign;
                else if (Halo3SeatViewWrite(at, replacement) && Halo3SeatViewRead(at, check) &&
                         !memcmp(check, replacement, n))
                {
                    FlushInstructionCache(GetCurrentProcess(), at, n);
                    result = Halo3SeatViewSwap::Done;
                }
            }
            (void)frozen.Release();
        }
    }
    DWORD ignored = 0;
    VirtualProtect(at, n, previous, &ignored);
    return result;
}

// Every site not yet redirected gets its redirect, under ONE freeze of the other threads. Halo3SeatViewSwapBytes
// freezes the whole process for its one site, and an install redirects three, at a level's start, when a freeze is
// slowest and the game stalls for its length. Each site's result is the one Halo3SeatViewSwapBytes would give it:
// Done (redirected now, or already), Busy (no freeze, no write access, or a thread inside its bytes; nothing
// written there), Foreign (neither the game's call nor the redirect). A site found Foreign before the freeze means
// nothing is written at any site.
void Halo3SeatViewRedirectSites(Halo3SeatViewState& s, Halo3SeatViewSwap (&results)[seat_view::kDirectorSiteCount])
{
    constexpr size_t n = seat_view::kCallBytes;
    constexpr size_t count = seat_view::kDirectorSiteCount;
    bool candidate[count]{};
    bool writable[count]{};
    DWORD previous[count]{};
    bool foreign = false, any = false;
    for (size_t i = 0; i < count; ++i)
    {
        Halo3SeatViewSite& site = s.sites[i];
        results[i] = Halo3SeatViewSwap::Done;
        if (site.patched)
            continue;
        uint8_t now[n];
        if (!SeatAimFrameExecutable(site.call, n) || !Halo3SeatViewRead(site.call, now) ||
            (memcmp(now, site.redirect, n) && memcmp(now, site.stock, n)))
        {
            results[i] = Halo3SeatViewSwap::Foreign;
            foreign = true;
        }
        else if (memcmp(now, site.redirect, n))
        {
            results[i] = Halo3SeatViewSwap::Busy;
            candidate[i] = true;
        }
    }
    for (size_t i = 0; i < count && !foreign; ++i)
    {
        if (!candidate[i])
            continue;
        writable[i] = VirtualProtect(s.sites[i].call, n, PAGE_EXECUTE_READWRITE, &previous[i]) != FALSE;
        any = any || writable[i];
    }
    if (any)
    {
        ReachThreadFreeze frozen;
        if (frozen.Capture())
        {
            bool safe[count]{};
            for (size_t i = 0; i < count; ++i)
                safe[i] = writable[i];
            for (const ReachFrozenThread& thread : frozen.Threads())
            {
                if (!thread.suspended)
                    continue;
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (!GetThreadContext(thread.handle, &context))
                {
                    // A live thread whose place is unknown could be inside any of them.
                    if (WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                        for (bool& one : safe)
                            one = false;
                    continue;
                }
                for (size_t i = 0; i < count; ++i)
                {
                    const auto at = reinterpret_cast<DWORD64>(s.sites[i].call);
                    if (context.Rip > at && context.Rip < at + n)
                        safe[i] = false;
                }
            }
            for (size_t i = 0; i < count; ++i)
            {
                if (!safe[i])
                    continue;
                uint8_t* const at = s.sites[i].call;
                uint8_t check[n];
                if (!Halo3SeatViewRead(at, check) || memcmp(check, s.sites[i].stock, n))
                    results[i] = Halo3SeatViewSwap::Foreign;
                else if (Halo3SeatViewWrite(at, s.sites[i].redirect) && Halo3SeatViewRead(at, check) &&
                         !memcmp(check, s.sites[i].redirect, n))
                {
                    FlushInstructionCache(GetCurrentProcess(), at, n);
                    results[i] = Halo3SeatViewSwap::Done;
                }
            }
            (void)frozen.Release();
        }
    }
    for (size_t i = 0; i < count; ++i)
    {
        DWORD ignored = 0;
        if (writable[i])
            VirtualProtect(s.sites[i].call, n, previous[i], &ignored);
    }
}

// No thread is inside the relay, and none can return into it: its own count is zero and, with every other thread
// suspended, no instruction pointer lies in it. Only meaningful once every site is back to stock.
bool Halo3SeatViewRelayQuiet(const Halo3SeatViewState& s)
{
    if (!s.relay)
        return true;
    if (InterlockedCompareExchange(&g_halo3SeatViewInside, 0, 0) != 0)
        return false;
    bool quiet = false;
    ReachThreadFreeze frozen;
    if (frozen.Capture())
    {
        quiet = InterlockedCompareExchange(&g_halo3SeatViewInside, 0, 0) == 0;
        const auto begin = reinterpret_cast<DWORD64>(s.relay);
        for (const ReachFrozenThread& thread : frozen.Threads())
        {
            if (!thread.suspended)
                continue;
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread.handle, &context))
            {
                if (WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                    quiet = false;
                continue;
            }
            if (context.Rip >= begin && context.Rip < begin + seat_view::kRelayBytes)
                quiet = false;
        }
        (void)frozen.Release();
    }
    return quiet;
}

void Halo3SeatViewReleaseRelay(Halo3SeatViewState& s)
{
    if (s.unwindRegistered && s.unwind)
        RtlDeleteFunctionTable(s.unwind);
    if (s.relay)
        VirtualFree(s.relay, 0, MEM_RELEASE);
    s.relay = nullptr;
    s.unwind = nullptr;
    s.unwindRegistered = false;
}

// The director is answered only where the tag is left alone (the default). With the tag written (the dev A/B
// key), the game answers first person by itself and the sites stay stock.
bool Halo3SeatViewWanted()
{
    return g_config.vehicle_first_person && (g_config.vehicle_hide_body || g_config.vehicle_cameras) &&
        !Halo3SeatViewWritesTag();
}

// Put every site that still holds this relay's redirect back to the game's own call. A site whose bytes are no
// longer the redirect (the module is gone, or someone else wrote there) is forgotten without a write, and `foreign`
// says so. True: a site could not be put back yet (no thread freeze, or a thread inside the bytes) and still
// calls the relay.
bool Halo3SeatViewRestoreSites(Halo3SeatViewState& s, bool& foreign)
{
    bool busy = false;
    for (Halo3SeatViewSite& site : s.sites)
    {
        if (!site.patched || !site.call)
            continue;
        const Halo3SeatViewSwap swap = Halo3SeatViewSwapBytes(site.call, site.redirect, site.stock);
        if (swap == Halo3SeatViewSwap::Busy)
            busy = true;
        else
        {
            site.patched = false;
            foreign = foreign || swap == Halo3SeatViewSwap::Foreign;
        }
    }
    return busy;
}

void Halo3SeatViewNotePending(Halo3SeatViewState& s, const char* what)
{
    if (!s.pendingLogged)
        LOG("Halo 3 seat view: %s is pending (thread snapshot unavailable or a thread inside the bytes); retrying",
            what);
    s.pendingLogged = true;
}

// WaitThread: bring the three sites in line with the setting. Cheap when nothing changes.
void Halo3SeatViewApply(Halo3SeatViewState& s)
{
    if (!s.relay)
        return;
    if (TitleAdapter_GetGeneration(GameTitle::Halo3) != s.generation)
    {
        // Another generation: the module these sites were resolved in is gone, replaced, or still mapped and
        // running under a new generation. Whichever it is, a site that still holds the redirect is put back first
        // (LIFETIME above); only then is the relay released, and only if nothing can still be inside it. Otherwise
        // it is left (4 KiB) rather than risked.
        g_halo3SeatViewRedirected.store(false, std::memory_order_release);
        bool foreign = false;
        if (Halo3SeatViewRestoreSites(s, foreign))
        {
            Halo3SeatViewNotePending(s, "the title generation changed; putting the director's call sites back");
            return;
        }
        const bool quiet = Halo3SeatViewRelayQuiet(s);
        LOG("Halo 3 seat view: the title generation changed; no call site of the director goes through the relay "
            "any more%s", quiet ? ", and it is released" : " (the relay is left in place: a thread is inside it)");
        if (quiet)
            Halo3SeatViewReleaseRelay(s);
        s = {};
        return;
    }
    const bool want = !s.standDown && !s.retired && Halo3SeatViewWanted();
    bool all = true, any = false;
    for (const Halo3SeatViewSite& site : s.sites)
    {
        all = all && site.patched;
        any = any || site.patched;
    }
    if (want ? all : !any)
    {
        // Every site calls the relay (also a module taken up again after a teardown that could not finish).
        if (want)
            g_halo3SeatViewRedirected.store(true, std::memory_order_release);
        // A teardown that ran out of time is finished here: every site is back, so the relay goes as soon as
        // nothing is inside it.
        if (s.retired && Halo3SeatViewRelayQuiet(s))
        {
            Halo3SeatViewReleaseRelay(s);
            LOG("Halo 3 seat view: the director's calls are restored and the relay released");
            s = {};
        }
        return;
    }
    if (!want)
    {
        g_halo3SeatViewRedirected.store(false, std::memory_order_release);
        bool foreign = false;
        const bool busy = Halo3SeatViewRestoreSites(s, foreign);
        if (foreign && !s.standDown && !s.retired)
        {
            // Someone else's bytes now: that site is not ours to write either way. Nothing more is redirected in
            // this module.
            LOG("Halo 3 seat view: a call site of the director is no longer the redirect this mod wrote; it is "
                "left as it is, and seats keep the game's third-person view in this module");
            s.standDown = true;
        }
        if (busy)
        {
            Halo3SeatViewNotePending(s, "putting the director's call sites back");
            return;
        }
        s.pendingLogged = false;
        if (!s.retired)
            LOG("Halo 3 seat view: the director's calls are back to the game's own (%s)",
                s.standDown ? "a call site is no longer this mod's"
                : !g_config.vehicle_first_person || !g_config.vehicle_hide_body
                    ? "vehicle_first_person / vehicle_hide_body off"
                    : "seat_view_tag_write = 1: the seat's tag flag is written instead (dev A/B)");
        return;
    }
    bool busy = false;
    Halo3SeatViewSwap swaps[seat_view::kDirectorSiteCount];
    Halo3SeatViewRedirectSites(s, swaps);
    // Every site written is ours to put back, whatever another site turned out to be.
    for (size_t i = 0; i < seat_view::kDirectorSiteCount; ++i)
        if (swaps[i] == Halo3SeatViewSwap::Done)
            s.sites[i].patched = true;
    for (size_t i = 0; i < seat_view::kDirectorSiteCount; ++i)
    {
        const Halo3SeatViewSwap swap = swaps[i];
        if (swap == Halo3SeatViewSwap::Busy)
        {
            busy = true;
            continue;
        }
        if (swap == Halo3SeatViewSwap::Foreign)
        {
            // Someone else's bytes now: this site is not ours to write. Stand down for this module; the next
            // polls put back the sites that are still ours. The relay stays: a site may still call it.
            LOG("Halo 3 seat view: the call at +0x%X (%s) is no longer the one verified at install; the director "
                "keeps the game's own answer and seats keep the game's third-person view",
                seat_view::kHalo3DirectorSites[i].anchorRva + seat_view::kHalo3DirectorSites[i].callOffset,
                seat_view::kHalo3DirectorSites[i].what);
            g_halo3SeatViewRedirected.store(false, std::memory_order_release);
            s.standDown = true;
            return;
        }
    }
    if (busy)
    {
        Halo3SeatViewNotePending(s, "redirecting the director's calls");
        return;
    }
    s.pendingLogged = false;
    g_halo3SeatViewRedirected.store(true, std::memory_order_release);
    LOG("Halo 3 seat view: the camera director is answered first person for the seat the mod's camera owns "
        "(its three calls at +0x212074, +0x2127EF and +0x212DCB go through a relay at %p). The vehicle's data, "
        "the game's own function and the simulation's call at +0x%X are untouched: shots, AI and every other "
        "player's game see a stock seat",
        static_cast<void*>(s.relay), seat_view::kHalo3EvaluatorCallRva);
}
} // namespace

// The seat sampler's side (halo3_native_seat_patch.inl), camera thread.
bool Halo3SeatViewAvailable()
{
    return g_halo3SeatViewRedirected.load(std::memory_order_acquire);
}
void Halo3SeatViewArm(int32_t unit)
{
    g_halo3SeatViewUnit.store(unit, std::memory_order_release);
}
void Halo3SeatViewDisarm()
{
    g_halo3SeatViewUnit.store(-1, std::memory_order_release);
    g_halo3SeatViewChaseUnit.store(-1, std::memory_order_release);
}
// The occupant a chase camera of the mod's shows: a "first person" answer becomes "third person" (-1 = nobody).
void Halo3SeatViewArmChase(int32_t unit)
{
    g_halo3SeatViewChaseUnit.store(unit, std::memory_order_release);
}
// DEV BUILDS ONLY: seat_view_tag_write = 1 puts back the write to the seat's tag flag, for an A/B in the headset.
// A player build never writes it.
bool Halo3SeatViewWritesTag()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    return g_config.seat_view_tag_write != 0;
#else
    return false;
#endif
}

// WaitThread, at title install: verify the three director sites and build the relay. False = the seat keeps the
// game's third-person view (and its drawn character); nothing was written.
bool Halo3SeatViewInstall(uintptr_t base, size_t size, uint32_t generation)
{
    Halo3SeatViewState& s = g_halo3SeatView;
    g_halo3SeatViewRetry = {};
    if (s.relay && s.base == base && s.generation == generation)
    {
        // The same module again (a teardown that could not finish, then a new install): the relay is taken up.
        s.retired = false;
        s.removeFailures = 0;
        Halo3SeatViewApply(s);
        return true;
    }
    if (s.relay)
        Halo3SeatViewApply(s);   // another generation's: its sites are put back and it is forgotten there
    if (s.relay)
    {
        // Still held (a site not yet put back, or a thread inside it). The poll takes this install up again.
        g_halo3SeatViewRetry = {base, size, generation};
        return false;
    }
    s = {};
    const char* why = nullptr;
    const seat_view::DirectorSite* failed = nullptr;
    const uintptr_t function = base + seat_view::kHalo3ThirdPersonRva;
    if (!base || !generation || seat_view::kHalo3ThirdPersonRva >= size)
        why = "the module is not loaded";
    for (size_t i = 0; !why && i < seat_view::kDirectorSiteCount; ++i)
    {
        const seat_view::DirectorSite& site = seat_view::kHalo3DirectorSites[i];
        uint8_t* const call = reinterpret_cast<uint8_t*>(base + site.anchorRva + site.callOffset);
        uintptr_t target = 0;
        failed = &site;
        if (site.anchorRva + site.callOffset + seat_view::kCallBytes > size)
            why = "a call site lies outside the module";
        else if (!SeatAimFrameUniqueAt(base, size, site.anchorRva, site.pattern))
            why = "the code around a call site is missing, ambiguous or moved";
        else if (!SeatAimFrameExecutable(call, seat_view::kCallBytes) || !Halo3SeatViewRead(call, s.sites[i].stock))
            why = "a call site is not in executable memory";
        else if (!seat_view::ReadCall(s.sites[i].stock, reinterpret_cast<uintptr_t>(call), target) ||
                 target != function)
            why = "a call site no longer calls the seat's third-person test";
        else
            s.sites[i].call = call;
    }
    if (!why)
    {
        failed = nullptr;
        s.relay = Halo3SeatViewAllocateNear(base + seat_view::kHalo3DirectorSites[0].anchorRva,
                                            kHalo3SeatViewRelayAllocation);
        if (!s.relay)
            why = "no memory within a call's reach of the module";
    }
    if (!why)
    {
        const size_t code = seat_view::BuildRelay(s.relay, kHalo3SeatViewUnwindInfoOffset, function,
            reinterpret_cast<uintptr_t>(&Halo3SeatViewAdjust), reinterpret_cast<uintptr_t>(&g_halo3SeatViewInside));
        const size_t unwind = seat_view::BuildRelayUnwind(s.relay + kHalo3SeatViewUnwindInfoOffset,
                                                          seat_view::kRelayUnwindBytes);
        bool built = code == seat_view::kRelayBytes && unwind == seat_view::kRelayUnwindBytes;
        for (size_t i = 0; built && i < seat_view::kDirectorSiteCount; ++i)
            built = seat_view::BuildCall(reinterpret_cast<uintptr_t>(s.sites[i].call),
                                         reinterpret_cast<uintptr_t>(s.relay), s.sites[i].redirect);
        if (built)
        {
            // The relay's function-table entry, in the same allocation and written while it is still writable.
            s.unwind = reinterpret_cast<RUNTIME_FUNCTION*>(s.relay + kHalo3SeatViewUnwindEntryOffset);
            s.unwind->BeginAddress = 0;
            s.unwind->EndAddress = static_cast<DWORD>(seat_view::kRelayBytes);
            s.unwind->UnwindData = static_cast<DWORD>(kHalo3SeatViewUnwindInfoOffset);
        }
        DWORD previous = 0;
        if (!built)
            why = "the relay could not be built within reach";
        else if (!VirtualProtect(s.relay, kHalo3SeatViewRelayAllocation, PAGE_EXECUTE_READ, &previous))
            why = "the relay's memory could not be made executable";
        else
        {
            FlushInstructionCache(GetCurrentProcess(), s.relay, kHalo3SeatViewRelayAllocation);
            // Registered so a stack walk or an exception passing through the relay unwinds it exactly.
            s.unwindRegistered = RtlAddFunctionTable(s.unwind, 1, reinterpret_cast<DWORD64>(s.relay)) != 0;
        }
        if (why)
            Halo3SeatViewReleaseRelay(s);
    }
    if (why)
    {
        if (failed)
            LOG("Halo 3 seat view: the director is NOT answered (%s: %s, +0x%X); a seat keeps the game's "
                "third-person view and its drawn character. Nothing was written",
                why, failed->what, failed->anchorRva + failed->callOffset);
        else
            LOG("Halo 3 seat view: the director is NOT answered (%s); a seat keeps the game's third-person view "
                "and its drawn character. Nothing was written", why);
        s = {};
        return false;
    }
    s.base = base;
    s.generation = generation;
    if (!Halo3SeatViewWanted())
        LOG("Halo 3 seat view: the director's three call sites are verified but not redirected (%s)",
            !g_config.vehicle_first_person || !g_config.vehicle_hide_body
                ? "vehicle_first_person / vehicle_hide_body off"
                : "seat_view_tag_write = 1: the seat's tag flag is written instead (dev A/B)");
    Halo3SeatViewApply(s);
    return true;
}

// WaitThread, at title teardown: the sites go back to the game's own calls (only over our own bytes), then the
// relay is released once nothing is inside it. False = try again. After kHalo3SeatViewRemoveTries the teardown is
// let through with the state kept and marked retired (LIFETIME above): nothing a site may still call is released,
// and nothing that names a patched site is forgotten.
constexpr uint32_t kHalo3SeatViewRemoveTries = 40;
bool Halo3SeatViewRemove()
{
    Halo3SeatViewState& s = g_halo3SeatView;
    Halo3SeatViewDisarm();
    g_halo3SeatViewRedirected.store(false, std::memory_order_release);
    g_halo3SeatViewRetry = {};
    if (!s.relay)
    {
        s = {};
        return true;
    }
    if (s.retired)
        return true;   // already let through once; the poll is finishing it
    bool foreign = false;
    const bool busy = Halo3SeatViewRestoreSites(s, foreign);
    if (!busy && Halo3SeatViewRelayQuiet(s))
    {
        Halo3SeatViewReleaseRelay(s);
        LOG("Halo 3 seat view: the director's calls are restored and the relay released at teardown");
        s = {};
        return true;
    }
    if (++s.removeFailures < kHalo3SeatViewRemoveTries)
        return false;
    LOG("Halo 3 seat view: the relay could not be retired at teardown (%s). It is left in place and answers the "
        "game's own answer; every poll goes on putting the director's calls back, and releases it when it can",
        busy ? "a call site could not be put back" : "a thread is still inside it");
    s.retired = true;
    s.removeFailures = 0;
    return true;
}

// WaitThread poll: follow F1 / config changes, and (dev) say what the director was told.
void Halo3SeatViewSync()
{
    Halo3SeatViewApply(g_halo3SeatView);
    // An install that waited for another generation's relay: taken up once that relay is gone, for the generation
    // it was asked for and no other.
    if (!g_halo3SeatView.relay && g_halo3SeatViewRetry.generation &&
        g_halo3SeatViewRetry.generation == TitleAdapter_GetGeneration(GameTitle::Halo3))
    {
        const Halo3SeatViewRetry retry = g_halo3SeatViewRetry;
        (void)Halo3SeatViewInstall(retry.base, retry.size, retry.generation);
    }
    // One line each way, only once the game's own answer really was first person under a chase camera.
    static int32_t s_chaseLogged = -1;
    static uint32_t s_chaseAnswered = 0;
    const int32_t chase = g_halo3SeatViewChaseUnit.load(std::memory_order_relaxed);
    const uint32_t chaseAnswered = g_halo3SeatViewChaseAnswered.load(std::memory_order_relaxed);
    if (chase != -1 && chase != s_chaseLogged && chaseAnswered != s_chaseAnswered)
    {
        LOG("Halo 3 seat view: a chase camera shows a seat the game draws first person, so the director is told third "
            "person for unit 0x%08X: your character is drawn and the first-person arms and weapon are not",
            static_cast<uint32_t>(chase));
        s_chaseLogged = chase;
    }
    else if (chase == -1 && s_chaseLogged != -1)
    {
        LOG("Halo 3 seat view: the director has the game's own answer again for unit 0x%08X",
            static_cast<uint32_t>(s_chaseLogged));
        s_chaseLogged = -1;
    }
    s_chaseAnswered = chaseAnswered;
#if HALOMCCVR_DEV_DIAGNOSTICS
    static uint64_t s_lastMs = 0;
    static uint32_t s_asked = 0, s_answered = 0;
    const uint64_t now = GetTickCount64();
    if (now - s_lastMs < 5000)
        return;
    s_lastMs = now;
    const uint32_t asked = g_halo3SeatViewAsked.load(std::memory_order_relaxed);
    const uint32_t answered = g_halo3SeatViewAnswered.load(std::memory_order_relaxed);
    if (answered != s_answered)
        LOG("SEAT VIEW Halo 3 (dev): the director asked %u times in 5 s, %u answered otherwise than the game for "
            "unit 0x%08X (first person) / 0x%08X (third person, a chase camera)",
            asked - s_asked, answered - s_answered,
            static_cast<uint32_t>(g_halo3SeatViewUnit.load(std::memory_order_relaxed)),
            static_cast<uint32_t>(g_halo3SeatViewChaseUnit.load(std::memory_order_relaxed)));
    s_asked = asked;
    s_answered = answered;
#endif
}
