#pragma once
// DRIVE CASCADE, the DLL's side: opens a loaded game module for the direct drives' address resolution
// (src/common/drive_resolve.h, drive_known.h, drive_sites.h), runs a drive's resolution on it by the rungs under a
// structured-exception guard, keeps the address cache's file, says at each install which rung won and what the
// resolution cost, and watches a hook that is installed for calls that never come.
//
// A drive's install does this, and nothing else decides where it hooks or reads:
//
//     drive_cascade::Resolution<drive_sites::ReachSabre> found;
//     if (!drive_cascade::Resolve(base, size, {GameTitle::HaloReach, "pilot-controller"}, found,
//                                 [](drive_resolve::Resolver& r) { return ResolveReachSabre(r); }))
//         ... not hooked: found.why, or the report's first line that was not found ...
//     drive_cascade::Announce(drive_cascade::kReachSabreName, found);
//
// THE RUNGS (drive_known.h): the known addresses first, each verified where it stands with no search of the
// module -- the tables' recorded RVAs when the module is the build they were measured on, otherwise what this
// process already resolved in this very module, otherwise the RVAs a search of this same build saved -- and only
// when one of them fails, the search. What the search finds on a build the tables do not describe is saved, so
// the next launch starts from it. Every address the install then uses is one that was verified or found in THIS
// module.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "../common/addrcascade/AddressCascade.hpp"
#include "../common/drive_known.h"
#include "../common/drive_resolve.h"
#include "../common/runtime_types.h"

namespace drive_cascade
{
// Which drive: the title whose game DLL it is in, and its name in the address cache (one word).
struct Drive
{
    GameTitle title;
    const char* id;
};

// WHAT EACH DRIVE'S LINES BEGIN WITH IN THE LOG. Named once: an install says them, and the offline check
// (tools/drive_address_check.cpp) says with them the very line an install writes for a game DLL.
inline constexpr char kHalo2Name[] = "Halo 2 DRIVE";
inline constexpr char kReachName[] = "Reach direct-drive";
inline constexpr char kReachSabreName[] = "Reach Sabre direct drive";
inline constexpr char kHalo4FlightName[] = "Halo 4 Broadsword direct drive";
#if HALOMCCVR_DEV_DIAGNOSTICS
inline constexpr char kReachObserverName[] = "REACH SF: type-13 observer";
inline constexpr char kHalo4ObserverName[] = "H4 T13: type-13 observer";
#endif
// Halo 3's, ODST's and Halo 4's control update: `title` is the title's name in drive_sites::kLegacyDriveSites.
inline void LegacyName(const char* title, char* out, size_t size)
{
    std::snprintf(out, size, "DRIVE %s", title);
}

// What a resolution went through, beyond its report.
struct Rungs
{
    drive_known::Outcome outcome{};
    bool recordedBuild = false;          // the module is the build the drive's tables were measured on
    const char* savedRefused = nullptr;  // saved addresses were there for this drive and were not used: why
    bool saved = false;                  // what the search found was written to the cache
    const char* notSaved = nullptr;      // ... or why it was not, when it was worth saving
    // WHAT THIS ADDRESS RESOLUTION COST THE WORKER, in milliseconds: RunPlan from opening the module to the end
    // of the last rung, the cache's file included where it was opened. (How many times the whole module was
    // searched is outcome.moduleScans.) Measured once per resolution, and said in the drive's one line. It is
    // not the cost of the install that asked for it: see Announce.
    double ms = 0.0;
    // A RESULT THAT IS NOT WHOLE IS NOT BELIEVED FROM ONE SEARCH (drive_known::kFreshSearches).
    // `searchedAgain`: this resolution was a fresh one made because what the process remembered of the drive in
    // this module was not whole, and which of the kFreshSearches it was (0: it was not).
    // `searches`: when a result that is not whole was repeated without a search, how many searches of this
    // module had come to it (0: it was not).
    uint32_t searchedAgain = 0;
    uint32_t searches = 0;
};

// THE ADDRESS CACHE'S FILE: HaloMCCVR-drive-addresses.txt in the mod's own folder, beside its log and its
// settings. `folder` ends with a separator. Until this is called there is no file and nothing is read or saved.
// It is a cache and not a setting: written by the mod, verified before every use, safe to delete. A file that is
// there and could not be read (another program has it open), or that is larger than any this mod writes, is
// left as it is: nothing is taken from it and nothing is saved over it, and the drive's line says so. The
// setting drive_address_cache = 0 turns both the reading and the writing off.
void SetCacheFolder(const wchar_t* folder);
// The same for a file named outright (tests).
void SetCacheFile(const wchar_t* path);
// WHAT THIS PROCESS ALREADY RESOLVED. A drive is installed again whenever its title's core is -- at a level
// boundary, after a pause -- and the module it resolves in is the same one as before. So the last resolution of
// each drive is kept in memory with the module instance it was made in (its base, its build stamp and the
// title's module generation), and the next install of that drive in that module verifies it in place instead of
// resolving afresh: it costs no search and opens no file.
//
// A WHOLE result is repeated from the next install on. A result that is NOT whole -- a line missing, a refusal --
// is not believed from one search: the next installs resolve the drive afresh, drive_known::kFreshSearches times
// in that module instance, and a search that now finds everything takes its place. Only a miss that those
// searches repeat is taken for the module's own; from then on it is repeated without a search, the lines that
// were not found are not searched for again in that module, and the drive's line says so.
//
// Another base, stamp or generation, a plan that asks for other lines, a module that does not open, or one
// address that no longer holds its bytes, and what is remembered is dropped and the drive resolved afresh; a
// refusal is therefore never carried over a real reload of the DLL. Nothing of it outlives the process, and none
// of it is used or kept while a dev fault is set.
//
// Which image of a title's DLL is loaded: a number that changes when MCC unloads the DLL and loads it again, even
// at the same base (the title adapter's module generation). Until a source is named, a module instance is told
// from another by its base and build stamp alone.
void SetModuleGenerationSource(uint32_t (*generationOf)(GameTitle title));
// Forgets every remembered resolution, as a new process would have none (the tests and the offline check).
void ForgetResolutions();

// How many times the cache's file has been opened, to read or to write, since the process started. It is opened
// only when a drive's saved addresses are needed or are being saved: never for a drive that verified at the
// recorded addresses. (The tests and the offline check count on it.)
uint32_t CacheFileOpens();

#if HALOMCCVR_DEV_DIAGNOSTICS
// DEV BUILDS ONLY, for the tests. `probe` is called each time the cache's file is about to be opened, to read or
// to write, with the cache's lock held (null takes it away): a test raises an exception from it, to see that the
// lock is not left held. CacheLockHeld: whether the lock is held at this moment.
void SetCacheProbe(void (*probe)());
bool CacheLockHeld();
#endif

// Opens the module at `base` (its headers, the readable runs of its executable sections, its .pdata, the TLS
// index its PE TLS directory names; every byte read as the game has it, through this mod's own enabled hooks)
// and runs `plan` on it by the rungs. `keep` is called for the run whose result is the one in use. False when
// the module could not be opened, a read faulted, or the plan asks for more lines than a report holds: `why`
// says which, the report is empty, and the drive must not be hooked. Reads and writes the cache's file: call it
// only where a file may be read (the installs' own cold worker thread), never from a detour.
bool RunPlan(uintptr_t base, size_t size, const Drive& drive, drive_known::PlanFn plan, drive_known::KeepFn keep,
             void* context, drive_resolve::Report& report, Rungs& rungs, const char*& why);

// What one drive's resolution produced.
template <typename Found>
struct Resolution
{
    Found found{};
    drive_resolve::Report report{};
    Rungs rungs{};
    const char* why = nullptr;   // the module did not open, a read faulted, or the plan is too long for a report
};

// Runs `plan` (Found (drive_resolve::Resolver&)) on the module. False: nothing was resolved (out.why).
template <typename Found, typename Plan>
bool Resolve(uintptr_t base, size_t size, const Drive& drive, Resolution<Found>& out, Plan&& plan)
{
    struct Context
    {
        std::remove_reference_t<Plan>* plan;
        Found aside;
        Found* found;
    } context{&plan, Found{}, &out.found};
    return RunPlan(
        base, size, drive,
        [](drive_resolve::Resolver& resolver, void* raw) {
            auto* c = static_cast<Context*>(raw);
            c->aside = (*c->plan)(resolver);
        },
        [](void* raw) {
            auto* c = static_cast<Context*>(raw);
            *c->found = c->aside;
        },
        &context, out.report, out.rungs, out.why);
}

// The first line of a report that was not found, as "<name>: <verdict>[: <the proof that failed>]" -- what a
// drive names when it is not hooked. "nothing" when every line was found.
void FirstFailure(const drive_resolve::Report& report, char* out, size_t size);

// WHICH RUNG WON, AND WHAT THE ADDRESS RESOLUTION COST. One line per install of a drive: verified at the
// recorded addresses with no search; as this session already resolved it in this module; verified at the
// addresses saved from an earlier launch of this build; found by search (how many moved, and whether they were
// saved for the next launch); or which addresses were not found, which a repeat says again and does not pass
// over. A drive whose result was not whole says which of its fresh searches this was ("searched again, N of
// 3"), and after the last of them that what is missing is "not searched again in this module". Each says what
// resolving that drive's addresses cost the worker -- "(address resolution: N scans, T ms)": how many times
// the whole module was searched for them, and the time RunPlan took -- which is why the line is said at every
// install and not only when the rung changes: no two cost the same.
//
// IT IS THE COST OF THE ADDRESS RESOLUTION, NOT OF THE INSTALL. An install goes on to bind other things in its
// own code -- the seat frame reader, the occupant's aim vector -- and those search the whole module themselves,
// at every install, with their own scans. None of that is counted or timed here.
//
// An install is once per core of a title, never per poll. `drive`: the drive's name as its own log lines begin.
void Announce(const char* drive, const drive_resolve::Report& report, const Rungs& rungs);

template <typename Found>
void Announce(const char* drive, const Resolution<Found>& resolution)
{
    Announce(drive, resolution.report, resolution.rungs);
}

// "INSTALLED" IS NOT "RUNNING". MinHook installs on any address and reports success; the only proof that an
// address was the right one is the hook being called. One HookWatch per hook, passed over by the cold worker on
// every one of its loops, from the drive's own report. It counts only periods in which the call was POSSIBLE at
// every pass -- the drive's own word, from something its hook does not produce, that the game is in the state the
// hooked code serves -- and starts again from nothing whenever it was not, so a menu, a load or a cutscene never
// counts against a healthy address. Log-only: a drive whose hook does not run already leaves the aim to the stick
// servo through its heartbeat.
class HookWatch
{
public:
    // The possible, uncalled time after which the hook is called dead: kPeriods periods of kPeriodMs in a row.
    static constexpr uint32_t kPeriods = 5;
    static constexpr uint32_t kPeriodMs = 2000;
    static constexpr uint32_t kSeconds = kPeriods * kPeriodMs / 1000;

    // The number that names one install of a hook: where it is, and which install of it this is (0: there is
    // none). A hook removed and installed again at the same address is another install.
    static uint64_t Install(const void* target, uint32_t count)
    {
        return target ? (static_cast<uint64_t>(count) << 48) ^ reinterpret_cast<uintptr_t>(target) : 0;
    }

    // One pass of the cold worker. `install`: the hook's install (Install; 0: it is not installed; another number
    // than at the last pass: a new install, and the watch starts again). `possible`: at this pass the game is in
    // the state in which it calls the hooked code. `called`: the hook's own counters show a call; the drive
    // clears them when it installs, so a call counted before this install is not one of it. True exactly once
    // per install: the hook has stood through kPeriods periods in a row, every pass of which found its call
    // possible, and it has never been called.
    bool Pass(uint64_t install, bool possible, bool called, uint64_t nowMs);

    bool everCalled() const { return everCalled_; }

private:
    void Restart();

    addrcascade::HookWatchdog dog_{kPeriods};
    uint64_t install_ = 0;
    uint64_t periodStartMs_ = 0;
    bool periodPossible_ = false;
    bool everCalled_ = false;
};

// The one line a watch writes when it fires. `drive`: the drive's name as its own log lines begin (it names the
// title). `what`: the hooked code. `rva`: where in the title's module the hook is. `state`: the state the game
// was seen in all that time, in which it calls that code.
void SayNeverCalled(const char* drive, const char* what, uint32_t rva, const char* state);
// The same line for any hook: `feature` begins it, and `consequence` (one sentence) says what the player is left
// with while that hook does not run. SayNeverCalled is this with a drive's consequence.
void SayHookNeverCalled(const char* feature, const char* what, uint32_t rva, const char* state,
                        const char* consequence);

// A TITLE'S HOOKS, WATCHED AS ONE SET: one HookWatch per hook, passed over by the cold worker on every loop as a
// drive's is, and one line per install of the set that says how all of them stood. Each watch keeps the
// HookWatch rules: it counts only time its own `possible` held at every pass, and it fires once per install of its
// hook. Log-only; the board decides nothing.
class HookWatchBoard
{
public:
    static constexpr size_t kMaxWatches = 32;
    // The time the set's base state held, in all, after which the summary is said even if a watch is still
    // waiting for the state that calls its hook (a vehicle seat, in a level without vehicles).
    static constexpr uint64_t kSummaryAfterMs = 60000;

    enum class Verdict : uint8_t
    {
        NotInstalled,   // no hook at this pass
        Waiting,        // installed, not called yet, not called dead
        Called,         // the hook has been called since its install
        NeverCalled,    // the watch fired
    };

    // Every loop, before the passes. `install`: which install of the whole set this is (0: none is live). Another
    // number than at the last call starts every watch and the summary again.
    void Begin(uint32_t install);
    // One pass of watch `index`. `target`: where its hook is now (null: not installed; it may come and go within
    // one install of the set). True exactly once per install of that hook, as HookWatch::Pass.
    bool Pass(size_t index, const void* target, bool possible, bool called, uint64_t nowMs);
    // After the loop's passes. `inPlay`: the set's base state held at this pass. True exactly once per install of
    // the set: at the first pass at which every installed watch has settled (called, or called dead) or, failing
    // that, once `inPlay` has held for kSummaryAfterMs in all.
    bool SummaryDue(bool inPlay, uint64_t nowMs);
    Verdict VerdictOf(size_t index) const;
    // The summary, after `title`: "N watched, all called", or "N watched: C called; K NEVER CALLED: a, b;
    // W not yet in the state that calls them: c". `names[i]` names watch i; `count` watches are told.
    void Summary(const char* title, const char* const* names, size_t count, char* out, size_t size) const;

private:
    struct Slot
    {
        HookWatch watch;
        bool installed = false;
        bool fired = false;
    };
    Slot slots_[kMaxWatches];
    uint32_t install_ = 0;
    bool summarised_ = false;
    uint64_t inPlayMs_ = 0;
    uint64_t lastInPlayAtMs_ = 0;
};
} // namespace drive_cascade
