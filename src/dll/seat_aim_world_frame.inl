// Included in game.cpp at file scope, just before legacy_direct_drive.inl.
//
// SEATS AIM IN WORLD ANGLES (src/common/seat_aim_frame_logic.h has the mechanism; docs/ALL-TITLES-AIM-2026-09-29.md
// "Seats aim in world angles: the C20 flag and the vehicle-tilt rebase"). Halo 3 and ODST rotate a player's aim
// by the parent vehicle's up frame unless the occupied seat carries the `third person camera` flag (bit 4). Every
// stock player seat carries it, so stock never does; our C20 first-person seat patch clears it, which switches the
// rebase on and adds the hull's tilt to the world angles the seat direct drive writes. Here the bit-4 test's
// `jne skip` becomes `jmp skip; nop` while the title's first-person seat patch is enabled, so seats aim in world
// angles exactly as in stock, and the restore puts the jne back.
//
// WHY THIS MATCHES AN UNMODDED PEER. The skip is correct ONLY because every stock Halo 3 / ODST player seat carries
// bit 4 and it is our own C20 patch that clears it: an unmodded game, and every unmodded co-op peer, never runs the
// rebase for a player, so skipping it restores exactly what they compute. The assumption is checked live: when C20
// finds the occupied seat's ORIGINAL flags already lacking bit 4 (a custom or modded seat), it refuses that seat
// and publishes it (g_halo3SeatNativeNoThirdPerson / g_odstSeatNativeNoThirdPerson); this then logs the seat once
// and puts the game's rebase back while it is occupied, because there stock rebases too -- and the seat drive then
// writes R^-1 of the aim (seat_frame_reader.h, case b), which every peer's rebase turns back into it.
//
// REACH (slot 2) follows the same rule for the seats ITS patch touches: R-V27 clears bit 4 on
// `allows weapons` passenger seats, which keep the flag in stock, so the skip restores stock there (case a). Reach
// also has stock seats that natively lack the flag; the Reach sampler publishes one from R-V27's own lease
// (g_reachSeatNativeNoThirdPerson) and the skip stands down while it is occupied (case b, the inverse). Halo 2 and
// CE have the rebase too, but no patch of ours clears their flag: nothing to skip there.
//
// LIFETIME (Halo 3 / ODST; Reach the same, with R-V27's lease in C20's place): the skip lives while the seat patch
// is ENABLED (and that seat exception), not only while C20 is Active on a seat. Following Active would re-patch on every seat entry and exit from the WaitThread's 50 ms poll:
// C20 clears bit 4 on the camera thread at once, so the rebase would act on the first frames of every entry, and
// each toggle freezes every game thread for a code write -- a possible dropped VR frame per vehicle entry. While
// C20 is not Active the skip is inert (a stock seat has bit 4 and skips the rebase anyway; only a player's aim
// reaches 0x355E20 / 0x39A600: their only caller, the unit-aim build, passes the unit held by a players-array
// record, as tools/verify_seat_aim_world_frame.py proves), except on a natively bit-4-less seat,
// which is exactly the case handled above. It also needs the title's vehicle sampler BOUND
// (SeatAimFrameSamplerBound), since the sampler is what runs C20 and what reports such a seat: unbound, the skip
// stands down and the title is exactly stock. Bound is not running -- seat_aim_frame::Wanted says why the
// remaining window (the camera hook not sampling) is left open rather than gated on a heartbeat.
//
// ADDR-HYGIENE: guarded. The anchor must be unique in the module and sit at its pinned RVA, the six bytes at the
// jne must be exactly `0F 85 rel32` (or our own `E9 rel32 90`, left by an earlier install of this module) with the
// target at the pinned skip label, and the title generation must still be the install's. Anything else: nothing is
// patched, one LOG line says why, and the seat keeps the game's own rebase.
//
// THREADS. Install, the setting sync and the restore all run on the WaitThread (title install, its poll loop and
// RemoveInstalledGameHooks / DisableAndRemoveOdstHooks), so the state below needs no lock. Only the dev witness
// (the sim thread's notes, the WaitThread's report) crosses threads, through atomics.
//
// WRITING CODE OTHER THREADS MAY BE EXECUTING. The six bytes do not sit inside one aligned qword in ODST (0x39A6C4
// spans two), so a single atomic store cannot replace them, and a torn write would let the sim thread decode half
// a jne and half a jmp. The repo's live code patch (Halo4SetEffectsSuppressed) freezes every other thread
// (ReachThreadFreeze: all allocation before the first suspension) and refuses while any thread's RIP sits strictly
// inside the bytes being replaced; the same is done here. A thread suspended AT the jne simply runs the new
// instruction; no branch in the function targets jcc+5, so the nop is never an entry point either. The page
// protection changes before the freeze, nothing allocates or logs while threads are suspended, and the
// instruction cache is flushed before they resume.

namespace
{
struct SeatAimFrameState
{
    const seat_aim_frame::Site* site = nullptr;
    GameTitle title = GameTitle::None;
    uint32_t generation = 0;
    uintptr_t base = 0;
    uint8_t* jcc = nullptr;                       // resolved and verified; null = nothing to manage
    uint8_t stock[seat_aim_frame::kPatchBytes]{};
    uint8_t skip[seat_aim_frame::kPatchBytes]{};
    bool patched = false;                         // our bytes are in place
    bool pendingLogged = false;                   // a transition is waiting on the thread freeze (logged once)
    uint32_t removeFailures = 0;
};
// Slots: 0 Halo 3, 1 ODST, 2 Reach (R-V27's `allows weapons` passenger seats, case (a)).
constexpr int kSeatAimFrameSlots = 3;
SeatAimFrameState g_seatAimFrame[kSeatAimFrameSlots];

int SeatAimFrameSlot(GameTitle title)
{
    return title == GameTitle::Halo3 ? 0 : title == GameTitle::Halo3ODST ? 1 : title == GameTitle::HaloReach ? 2 : -1;
}

const char* SeatAimFrameSlotName(int slot)
{
    return slot == 0 ? "Halo 3" : slot == 1 ? "ODST" : "Reach";
}

// CO-OP LIMIT OF THE GLOBAL SKIP (docs/ALL-TITLES-AIM-2026-09-29.md "Co-op limit of
// the skip"). The jump skips the rebase for EVERY player unit this machine simulates, not only ours. When we host,
// a co-op peer sitting in a seat whose OWN flags lack bit 4 is not rebased on our host while the peer's own
// game rebases it. Stock Halo 3 / ODST player seats all carry bit 4, so there it needs custom content; stock Reach
// has such seats. Kept global on purpose: a per-unit skip needs a new code relay inside the rebase, which does not
// exist. Logged once per skip so a co-op aim report can be read against it.
void SeatAimFrameLogCoopLimit(int slot)
{
    static bool s_logged[3]{};
    if (slot < 0 || slot > 2 || s_logged[slot])
        return;
    s_logged[slot] = true;
    LOG("%s seat aim: NOTE (co-op host): the skip applies to every player unit this machine simulates; a peer in a "
        "seat whose own flags lack bit 4%s is not rebased here while their game rebases it (their aim can differ "
        "on our host while their vehicle tilts)", SeatAimFrameSlotName(slot),
        slot == 2 ? " (stock Reach has such seats)" : " (custom content only: every stock seat has bit 4)");
}

// The occupied seat natively lacks bit 4 (published by the C20 seat patch -- Reach: by its sampler from R-V27's
// lease; 0 = no). ODST's lives in the ODST bring-up block.
uint32_t SeatAimFrameNativeNoThirdPerson(int slot)
{
    if (slot == 0)
        return g_halo3SeatNativeNoThirdPerson.load(std::memory_order_relaxed);
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    if (slot == 1)
        return g_odstSeatNativeNoThirdPerson.load(std::memory_order_relaxed);
#endif
    if (slot == 2)
        return g_reachSeatNativeNoThirdPerson.load(std::memory_order_relaxed);
    return 0;
}

// The title's vehicle sampler is bound: exactly the gate Halo3SampleVehicleState / OdstSampleVehicleState check
// before doing anything (binding Installed, engine TLS index resolved). Only the sampler runs C20 and publishes a
// natively bit-4-less seat, so without it the skip stands down (seat_aim_frame::Wanted has the reasoning and the
// one gap left open).
bool SeatAimFrameSamplerBound(int slot)
{
    if (slot == 0)
        return g_halo3VehicleBinding.load(std::memory_order_acquire) ==
                   static_cast<uint8_t>(Halo3VehicleBindingState::Installed) &&
            g_engineTlsIndex;
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    if (slot == 1)
        return g_odstVehicleBinding.load(std::memory_order_acquire) ==
                   static_cast<uint8_t>(OdstVehicleBindingState::Installed) &&
            g_odstEngineTlsIndex;
#endif
    // Reach: the binding R-V27 itself gates on (ReachEnsureNativeFirstPersonSeat): the vehicle camera binding
    // Installed. It is also what runs the sampler that publishes a natively bit-4-less seat.
    if (slot == 2)
        return ReachSeatAimFrameBindingInstalled();
    return false;
}

bool SeatAimFrameWanted(int slot)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    const bool key = g_config.seat_aim_world_frame != 0;
#else
    const bool key = true;   // the A/B key exists in dev builds only
#endif
    // A SHARED SIMULATION (sight_layer.h Sight_FireTimeChangesAllowed false): no seat patch of ours clears a seat's
    // camera flag there (ODST's C20 and Reach's R-V27 stand down), so the skip has nothing to undo -- and left in
    // place it would keep the game from rebasing a seat that natively lacks the flag while every other machine
    // rebases it: the local player's for the ticks before the sampler has seen the seat, a peer's for as long as
    // they sit in it (docs/COOP-SHARED-SIMULATION-2026-10-13.md). The game's own rebase stays, and the seat drive
    // reads what it will do every update (seat_frame_reader.h).
    if (!Sight_FireTimeChangesAllowed())
        return false;
    if (slot == 2)
        return seat_aim_frame::ReachWanted(g_config.vehicle_first_person, key, SeatAimFrameSamplerBound(slot),
                                           SeatAimFrameNativeNoThirdPerson(slot) != 0);
    // Halo 3 leaves the seat's tag as loaded unless the dev A/B key asks for the write (halo3_seat_view.inl): with
    // bit 4 still set the game does not rebase, so there is nothing for the skip to undo and no instruction on the
    // simulation's path is touched. ODST still writes its tag.
    const bool tagWritten = g_config.vehicle_hide_body && (slot != 0 || Halo3SeatViewWritesTag());
    return seat_aim_frame::Wanted(g_config.vehicle_first_person, tagWritten, key,
                                  SeatAimFrameSamplerBound(slot), SeatAimFrameNativeNoThirdPerson(slot) != 0);
}

bool SeatAimFrameUniqueAt(uintptr_t base, size_t size, uint32_t rva, const char* pattern)
{
    const uintptr_t first = sig::Find(base, size, pattern);
    if (first != base + rva)
        return false;
    const uintptr_t next = first + 1;
    return next >= base + size || !sig::Find(next, static_cast<size_t>(base + size - next), pattern);
}

bool SeatAimFrameExecutable(const void* address, size_t bytes)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
        return false;
    const auto end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (reinterpret_cast<uintptr_t>(address) + bytes > end)
        return false;
    const DWORD protection = info.Protect & 0xFFu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool SeatAimFrameRead(const uint8_t* at, uint8_t out[seat_aim_frame::kPatchBytes])
{
    __try
    {
        memcpy(out, at, seat_aim_frame::kPatchBytes);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SeatAimFrameWrite(uint8_t* at, const uint8_t* bytes)
{
    __try
    {
        memcpy(at, bytes, seat_aim_frame::kPatchBytes);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

enum class SeatAimFrameSwap
{
    Done,
    Busy,      // the freeze could not be taken, or a thread sat inside the bytes: try again on the next poll
    Foreign,   // the bytes are neither the expected ones nor the replacement: not ours any more
};

// Replace `expected` with `replacement` at `at` while every other thread is frozen (see the header comment).
SeatAimFrameSwap SeatAimFrameSwapBytes(uint8_t* at, const uint8_t* expected, const uint8_t* replacement)
{
    constexpr size_t n = seat_aim_frame::kPatchBytes;
    uint8_t now[n];
    if (!SeatAimFrameExecutable(at, n) || !SeatAimFrameRead(at, now))
        return SeatAimFrameSwap::Foreign;
    if (!memcmp(now, replacement, n))
        return SeatAimFrameSwap::Done;
    if (memcmp(now, expected, n))
        return SeatAimFrameSwap::Foreign;
    DWORD previous = 0;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &previous))
        return SeatAimFrameSwap::Busy;
    SeatAimFrameSwap result = SeatAimFrameSwap::Busy;
    bool resumed = true;
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
                if (!SeatAimFrameRead(at, check) || memcmp(check, expected, n))
                    result = SeatAimFrameSwap::Foreign;
                else if (SeatAimFrameWrite(at, replacement) && SeatAimFrameRead(at, check) &&
                         !memcmp(check, replacement, n))
                {
                    FlushInstructionCache(GetCurrentProcess(), at, n);
                    result = SeatAimFrameSwap::Done;
                }
            }
            resumed = frozen.Release();
        }
    }
    DWORD ignored = 0;
    VirtualProtect(at, n, previous, &ignored);
    (void)resumed;
    return result;
}

const char* SeatAimFrameWhyOff(int slot)
{
    if (!Sight_FireTimeChangesAllowed())
        return "Co-op safe is on: no seat's camera flag is cleared, so the game's own rebase is every machine's";
    if (slot == 2 && !g_config.vehicle_first_person)
        return "vehicle_first_person off: R-V27 leaves every seat's third-person flag stock";
    if (slot != 2 && (!g_config.vehicle_first_person || !g_config.vehicle_hide_body))
        return "vehicle_first_person / vehicle_hide_body off: seats keep their stock third-person flag";
    if (slot == 0 && !Halo3SeatViewWritesTag())
        return "Halo 3 seats keep their tag flag as loaded (the camera director is answered instead), so the game "
               "does not rebase them";
    if (!SeatAimFrameSamplerBound(slot))
        return slot == 2 ? "the Reach vehicle camera binding is not installed: R-V27 patches no seat flag"
                         : "the vehicle sampler is not bound: no seat is checked, and no seat flag is patched";
    if (SeatAimFrameNativeNoThirdPerson(slot))
        return "the occupied seat natively lacks bit 4, so stock rebases it too";
#if HALOMCCVR_DEV_DIAGNOSTICS
    return "seat_aim_world_frame = 0 (dev A/B)";
#else
    return "unknown";   // unreachable: a player build has no A/B key, so Wanted fails only for a reason above
#endif
}

// WaitThread: bring the bytes in line with the setting. Cheap when nothing changes (no engine access at all).
void SeatAimFrameApply(int slot, SeatAimFrameState& s)
{
    if (!s.jcc)
        return;
    if (TitleAdapter_GetGeneration(s.title) != s.generation)
    {
        // The module this was resolved in is gone or replaced; its bytes are not ours to touch.
        LOG("%s seat aim: the title generation changed; forgetting the rebase patch site without writing",
            s.site->name);
        s = {};
        return;
    }
    const bool want = SeatAimFrameWanted(slot);
    if (want == s.patched)
        return;
    const SeatAimFrameSwap swap = want ? SeatAimFrameSwapBytes(s.jcc, s.stock, s.skip)
                                       : SeatAimFrameSwapBytes(s.jcc, s.skip, s.stock);
    if (swap == SeatAimFrameSwap::Busy)
    {
        if (!s.pendingLogged)
            LOG("%s seat aim: the rebase %s is pending (thread snapshot unavailable or a thread inside the "
                "bytes); retrying", s.site->name, want ? "skip" : "restore");
        s.pendingLogged = true;
        return;
    }
    s.pendingLogged = false;
    if (swap == SeatAimFrameSwap::Foreign)
    {
        LOG("%s seat aim: the bytes at +0x%X are no longer the ones verified at install; left alone, the seat "
            "keeps whatever the game now has", s.site->name, s.site->jccRva);
        s = {};
        return;
    }
    s.patched = want;
    if (want)
    {
        LOG("%s seat aim: the vehicle-tilt rebase is skipped (0x%X), seats aim in world angles as in stock",
            s.site->name, s.site->jccRva);
        SeatAimFrameLogCoopLimit(slot);
    }
    else
        LOG("%s seat aim: the vehicle-tilt rebase is back in place (0x%X; %s)", s.site->name, s.site->jccRva,
            SeatAimFrameWhyOff(slot));
}

} // namespace

bool SeatAimFrameInstall(GameTitle title, uintptr_t base, size_t size, uint32_t generation)
{
    const int slot = SeatAimFrameSlot(title);
    if (slot < 0)
        return false;
    SeatAimFrameState& s = g_seatAimFrame[slot];
    const seat_aim_frame::Site& site = slot == 0 ? seat_aim_frame::kHalo3Site
                                     : slot == 1 ? seat_aim_frame::kOdstSite
                                                 : seat_aim_frame::kReachSite;
    if (s.jcc && s.base == base && s.generation == generation)
    {
        SeatAimFrameApply(slot, s);
        return true;
    }
    s = {};
    const char* why = nullptr;
    uint8_t now[seat_aim_frame::kPatchBytes]{};
    uint8_t* jcc = reinterpret_cast<uint8_t*>(base + site.jccRva);
    if (!base || !generation || site.jccRva + seat_aim_frame::kPatchBytes > size)
        why = "the module is not loaded";
    else if (!SeatAimFrameUniqueAt(base, size, site.anchorRva, site.anchorPattern))
        why = "the seat-flag test's anchor is missing, ambiguous or moved";
    else if (!SeatAimFrameExecutable(jcc, seat_aim_frame::kPatchBytes) || !SeatAimFrameRead(jcc, now))
        why = "the jne is not in executable memory";
    else if (seat_aim_frame::BuildSkipPatch(now, site.jccRva, site.skipRva, s.skip))
        memcpy(s.stock, now, sizeof(now));
    else if (seat_aim_frame::BuildStockJcc(now, site.jccRva, site.skipRva, s.stock))
    {
        // An earlier install of this same module left our jump in place (a restore that gave up): adopt it.
        memcpy(s.skip, now, sizeof(now));
        s.patched = true;
    }
    else
        why = "the six bytes there are neither the stock `0F 85` nor our jump to the expected skip label";
    if (why)
    {
        s = {};
        LOG("%s seat aim: the vehicle-tilt rebase is NOT skipped (%s at +0x%X); seats keep the game's rebase",
            site.name, why, site.anchorRva);
        return false;
    }
    s.site = &site;
    s.title = title;
    s.generation = generation;
    s.base = base;
    s.jcc = jcc;
    const bool want = SeatAimFrameWanted(slot);
    if (!want && !s.patched)
        LOG("%s seat aim: the rebase skip is verified at 0x%X but not applied (%s)", site.name, site.jccRva,
            SeatAimFrameWhyOff(slot));
    else if (want && s.patched)
    {
        LOG("%s seat aim: the vehicle-tilt rebase is skipped (0x%X), seats aim in world angles as in stock",
            site.name, site.jccRva);
        SeatAimFrameLogCoopLimit(slot);
    }
    SeatAimFrameApply(slot, s);
    return true;
}

// WaitThread, at title teardown: put the stock jne back, but only over our own bytes. False = try again (the
// thread freeze was unavailable); after a few tries the patch is left in place -- it is behaviour-neutral once the
// seat flags are restored, and a later install of the same module adopts it.
bool SeatAimFrameRemove(GameTitle title)
{
    const int slot = SeatAimFrameSlot(title);
    if (slot < 0)
        return true;
    SeatAimFrameState& s = g_seatAimFrame[slot];
    if (!s.jcc)
    {
        s = {};
        return true;
    }
    if (!s.patched)
    {
        s = {};
        return true;
    }
    const SeatAimFrameSwap swap = SeatAimFrameSwapBytes(s.jcc, s.skip, s.stock);
    if (swap == SeatAimFrameSwap::Busy && ++s.removeFailures < 20)
        return false;
    if (swap == SeatAimFrameSwap::Done)
        LOG("%s seat aim: the vehicle-tilt rebase is restored at teardown (0x%X)", s.site->name, s.site->jccRva);
    else
        LOG("%s seat aim: the rebase skip could not be restored at teardown (%s); left in place, harmless with "
            "stock seat flags", s.site->name, swap == SeatAimFrameSwap::Busy ? "no thread freeze" : "foreign bytes");
    s = {};
    return true;
}

// WaitThread poll: follow F1 / config changes of the first-person seat settings (and the dev A/B key), and a seat
// that natively lacks bit 4 (logged once per seat, whether or not the skip is installed).
void SeatAimFrameSync()
{
    static uint32_t s_loggedNoThirdPerson[kSeatAimFrameSlots]{};
    for (int slot = 0; slot < kSeatAimFrameSlots; ++slot)
    {
        const uint32_t key = SeatAimFrameNativeNoThirdPerson(slot);
        if (key && key != s_loggedNoThirdPerson[slot])
        {
            s_loggedNoThirdPerson[slot] = key;
            LOG("%s seat aim: seat %u of vehicle definition 0x%X natively lacks bit 4: the skip changes stock "
                "behaviour here, so the game's rebase is put back while it is occupied (the first-person seat patch "
                "also leaves this seat stock; the seat drive writes the inverse of the rebase, case b)",
                SeatAimFrameSlotName(slot), key & 0xFFu, (key >> 8) & 0xFFFFu);
        }
        SeatAimFrameApply(slot, g_seatAimFrame[slot]);
    }
}

// The dev frame check is every title's and lives in seat_frame_reader.h
// (seat_frame_rt::NoteFrameCheck), printed in each drive's seats line.
