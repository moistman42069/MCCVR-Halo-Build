// addrcascade -- verification harness for resolved memory addresses.
//
// SPDX-License-Identifier: MIT
//
// SELF-CONTAINED ON PURPOSE. Nothing in here includes a game header, an engine header, or anything
// from the host project: it depends on <cstdint>, <cstddef> and (in the .cpp) <Windows.h>. Lifting
// the addrcascade\ folder into another codebase should require setting a log sink and nothing else.
// See README.md in this folder before changing that.
//
// ---------------------------------------------------------------------------------------------
// WHAT PROBLEM THIS SOLVES
//
// Any RVA or struct offset written into source is a measurement of ONE build. It rots when the
// target ships a patch and was never valid for a different store's binary of the same game. The
// failure is silent: hooking is positional, so installing on a stale address SUCCEEDS and reports
// success, and reads of unrelated memory usually pass a range check.
//
// The normal answer is a cascade -- try a signature, fall back to a recorded address, fall back to
// some other resolver, finally give up into a degraded-but-working mode. The problem with cascades
// is that every rung below the first only ever executes when something is already broken, so on a
// healthy machine NONE of them run and they rot unobserved. Building the fallback is not the same
// as knowing it works.
//
// So this library is deliberately NOT another pattern scanner (kananlib and friends already do that
// better). It is the VERIFICATION layer that tends to be missing:
//
//   Signature          find by shape, and refuse an AMBIGUOUS match rather than taking the first
//   scan_string /      anchor on a string literal or on the code that REFERENCES a known address --
//     scan_reference     both survive recompilation better than a raw byte pattern
//   FaultMask          force any single rung to fail, on a healthy machine, so the rung BELOW it
//                        can be watched doing its job
//   HookWatchdog       "installed" is not "running" -- catch an address that is never called
//   CoVariation        validate a DATA offset, which no code-signature check can do
//
// ---------------------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>

namespace addrcascade {

// ------------------------------------------------------------------ logging
//
// The host owns logging. Set this once at startup; every diagnostic below routes through it. A
// library that resolves addresses silently is exactly the failure mode this exists to prevent, so
// there is no "quiet" mode -- if you do not set a sink, the messages are dropped and you have
// given up the only evidence you were going to get.
using LogFn = void (*)(const char* message);
void set_logger(LogFn fn);
void logf(const char* fmt, ...);

// ------------------------------------------------------------------ module facts
//
// The PE walk every one of these needs is the same six lines, and hand-copying it is how one copy
// ends up without its IsBadReadPtr guard. All of these validate the headers before touching
// anything and return false rather than faulting on a module that is not what was expected.

struct ModuleRange {
    uintptr_t base = 0;
    uintptr_t end  = 0;   // base + SizeOfImage
    size_t    size = 0;
};
bool module_range(void* module_base, ModuleRange* out);

// WHICH BUILD IS THIS? The single most useful line to have in a log before any address is trusted,
// because it is the first thing a field report has to establish and the hardest to get afterwards.
// SizeOfImage alone separates most builds; the CodeView GUID + age is the identity the linker
// actually stamped, so a known-good list can be kept against it rather than against sizes that
// could collide.
struct ModuleIdentity {
    uint32_t size_of_image = 0;
    uint32_t timestamp     = 0;
    uint32_t pdb_age       = 0;
    char     pdb_guid[40]  = {};   // "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX", or "unknown"
};
bool module_identity(void* module_base, ModuleIdentity* out);

// The loader-assigned TLS slot index, read from IMAGE_TLS_DIRECTORY64::AddressOfIndex -- the
// canonical source for a number that is otherwise transcribed into source as a constant and rots.
//
// VALIDATE IT AS A RANGE, NEVER AS AN EQUALITY. The index is assigned per PROCESS: the same binary
// on the same machine has been observed using 76 and 84 on different runs. Only the RVA it lives at
// is a property of the build, and that is what out_rva is for.
bool tls_index(void* module_base, uint32_t* out_index, uintptr_t* out_rva);

// How many bytes from p are committed and readable, capped at `limit`. Zero means "do not touch".
//
// IsBadReadPtr cannot answer this: it reports a yes/no for a size you already guessed, and probing
// a size that straddles the end of a region is itself the fault you were trying to avoid. Every
// caller that wants to read a variable-length object from an address it did not allocate needs the
// real committed extent instead.
size_t readable_bytes(const void* p, size_t limit);

// ------------------------------------------------------------------ signatures
//
// mask: 'x' matches the byte in `bytes`, '?' is a wildcard. Keep them paired with a static_assert
// at the definition site (sizeof(sig) == sizeof(mask) - 1) -- a mask one character short silently
// narrows the pattern and is very hard to see by eye.
struct Signature {
    const unsigned char* bytes;
    const char*          mask;
    size_t               size;
};

struct ScanResult {
    uintptr_t address = 0;   // first match, or 0
    size_t    matches = 0;   // TOTAL matches found; 1 is the only trustworthy answer

    bool unique() const { return matches == 1 && address != 0; }
};

// Scans the module's executable sections. Counts EVERY match rather than returning the first:
// a signature that matches twice describes a code idiom, not a function, and taking the first hit
// would silently pick one at random on the next build.
ScanResult scan_signature(void* module_base, const Signature& sig);

// Anchor techniques that survive a recompile better than a byte pattern, because they key on
// content the compiler does not reorder. Both return 0 when not found or not unique.
//
// scan_string      -- address of the string's bytes in the module's data.
// scan_reference   -- address of an instruction whose RIP-relative operand points at `target`.
//                     The classic robust locate is: find a string, then find who references it.
uintptr_t scan_string(void* module_base, const char* str);
uintptr_t scan_reference(void* module_base, uintptr_t target);

// Proves the two anchor scanners above actually work, against a module you control -- pass your OWN
// module handle. It looks for a marker string this library embeds, then for the code that
// references it, and logs what it found either way.
//
// This exists because those two scanners are OFFERED capability with no consumer yet, and a library
// whose entire premise is "untested fallback code is a guess" must not ship two untested scanners.
// Call it once from a dev build. Returns false only if the marker string itself was not found,
// which means scanning is broken; a non-unique reference count is reported, not failed, because
// how many times a compiler references a literal is not this library's business.
bool self_test(void* own_module_base);

// ------------------------------------------------------------------ fault injection
//
// Compile OUT of shipping builds by leaving ADDRCASCADE_FAULTS undefined (the default). The host's
// dev build defines it as 1. Guard every injection site with `if (addrcascade::fault(BIT))` so the
// call vanishes entirely in a release build.
//
// WHY THIS EXISTS: the fallbacks only run when something is already broken, i.e. never on a working
// machine. Untested recovery code is a guess. Forcing the fault is the only way to watch the
// recovery actually recover.
void set_fault_mask(int mask);
int  fault_mask();

#if defined(ADDRCASCADE_FAULTS) && ADDRCASCADE_FAULTS
inline bool fault(int bit) { return (fault_mask() & bit) != 0; }
#else
inline bool fault(int)     { return false; }
#endif

// ------------------------------------------------------------------ "installed" is not "running"
//
// A hook installs successfully on ANY readable address. The only proof the address was right is the
// hook being CALLED. This watchdog counts toward a verdict of "dead", and the API is shaped to make
// the one mistake that matters hard to commit:
//
//   tick() REQUIRES you to say whether the watched call was POSSIBLE this tick.
//
// That parameter is not optional bookkeeping. A watchdog that counts from installation rather than
// from the first moment the call could happen will condemn a perfectly healthy address -- measured
// on a real title: hook installed at the main menu, alarm fired 11 s before gameplay even started,
// on every single launch. Counting only possible time is the whole difference between a diagnostic
// and a liar.
class HookWatchdog {
public:
    // ticks_until_dead is in units of YOUR OWN tick, whatever that is. State the cadence at the
    // call site: a timeout expressed in "ticks" is meaningless without it, and getting it wrong
    // fails silently in the safe-looking direction (the alarm simply never fires).
    explicit HookWatchdog(uint32_t ticks_until_dead) : m_limit(ticks_until_dead) {}

    // event_possible -- could the hooked code have been called during this tick? Menus, load
    //                   screens and modes where the subsystem stands down are all "no".
    // event_happened -- has the hook EVER been called (a latch, not a per-tick flag).
    //
    // Returns true on the single tick the verdict flips to dead, so the caller can log once.
    bool tick(bool event_possible, bool event_happened);

    bool     dead() const   { return m_dead; }
    uint32_t counted() const { return m_counted; }   // ticks of POSSIBLE time counted so far

    void reset() { m_counted = 0; m_dead = false; }

private:
    uint32_t m_limit   = 0;
    uint32_t m_counted = 0;
    bool     m_dead    = false;
};

// ------------------------------------------------------------------ validating a DATA offset
//
// Signature scanning validates CODE. Nothing in it says the struct offset you then write through is
// still the field you think it is -- and a field that moved by 4 bytes passes every pointer check,
// every range check, and a read-back of your own write.
//
// The check that does work: a field that holds a quantity must MOVE WHEN THAT QUANTITY MOVES.
// Feed samples of (candidate, reference) taken WHILE YOU ARE NOT WRITING the candidate. If the
// reference swings and the candidate sits still, the offset is not what you think.
//
// Sampling while you are driving the candidate proves nothing: you moved it.
class CoVariation {
public:
    struct Config {
        // How much total reference movement to require before any verdict. Too small and idle
        // noise decides it; too large and the check never completes.
        double reference_motion_required = 5.0;
        // Candidate movement, as a fraction of reference movement, below which it is a MISMATCH.
        // Generous by default: mirrors legitimately lag, scale and offset differently.
        double candidate_motion_floor = 0.25;
        // Give up with Inconclusive after this many samples, so a player standing still does not
        // leave the caller waiting forever.
        uint32_t max_samples = 600;
        // Wrap period for angular quantities (360.0 for degrees, 0.0 for linear). Without this a
        // 359 -> 1 step reads as 358 degrees of motion and the check is nonsense.
        double wrap = 0.0;
    };

    enum class Verdict { Pending, Match, Mismatch, Inconclusive };

    explicit CoVariation(const Config& cfg) : m_cfg(cfg) {}

    Verdict sample(double candidate, double reference);
    void    reset();

    double   reference_motion() const { return m_ref_motion; }
    double   candidate_motion() const { return m_cand_motion; }
    uint32_t samples() const          { return m_samples; }

private:
    Config   m_cfg{};
    bool     m_have_last  = false;
    double   m_last_cand  = 0.0;
    double   m_last_ref   = 0.0;
    double   m_cand_motion = 0.0;
    double   m_ref_motion  = 0.0;
    uint32_t m_samples     = 0;
};

// ------------------------------------------------------------------ validating a DATA offset, harder
//
// CoVariation above asks whether the field MOVES when the quantity moves. That catches a field that
// has gone dead, and it is the right tool when you have no way to predict the value. When you CAN
// predict it, it is far too weak, and weak in the direction that matters:
//
//   the likeliest consequence of a target patch is not that your offset lands on something dead --
//   it is that a field was inserted and your offset now lands on the NEIGHBOURING field of the same
//   hot struct, which moves just as much as the one you wanted.
//
// So when the candidate should EQUAL a reference you already have (after whatever transform relates
// them), test that instead. Measured on a real target: the correct field agreed with its reference
// to 0.01 degrees, while the neighbouring field 4 bytes away differed by 139. That is not a
// marginal signal.
//
// Feed it values in the SAME convention -- do the negation/scaling/unit conversion before calling.
// Sample while you are NOT driving the candidate, for the same reason as CoVariation: if you are
// writing it, you are testing your own arithmetic.
class ValueAgreement {
public:
    struct Config {
        // Max |candidate - reference| still considered agreement. Set it from the observed healthy
        // error with generous margin, NOT from what feels tidy: the failure it must separate from
        // is a different field entirely, which is usually wrong by tens or hundreds of units.
        double tolerance = 5.0;
        // The window must see this much reference movement before a Match is credited. A static
        // window can agree by coincidence -- two fields that happen to hold the same value while
        // nothing is happening prove nothing. Set 0 for a quantity that legitimately sits still.
        double reference_motion_required = 10.0;
        uint32_t min_samples = 16;
        uint32_t max_samples = 600;
        // Consecutive out-of-tolerance samples before declaring Mismatch. One bad sample is a
        // transient (a frame where the two are read a tick apart); several in a row is a fact.
        uint32_t strikes_to_fail = 8;
        double   wrap = 0.0;   // 360.0 for degrees; 0 for linear quantities
    };

    enum class Verdict { Pending, Match, Mismatch, Inconclusive };

    explicit ValueAgreement(const Config& cfg) : m_cfg(cfg) {}

    Verdict sample(double candidate, double reference);
    void    reset();

    double   worst_error() const      { return m_worst; }
    double   reference_motion() const { return m_ref_motion; }
    uint32_t samples() const          { return m_samples; }

    // THE NUMBER TO TUNE strikes_to_fail AGAINST -- longest RUN of consecutive out-of-tolerance
    // samples seen. Measured on a real target, the peak error was useless as a discriminator:
    // healthy play spiked to 56 deg while a genuinely wrong field sat at 82-94, which is not enough
    // separation to trust. Duration separates them cleanly instead, because a wrong field disagrees
    // on EVERY sample indefinitely while a healthy excursion is a transient. Report this from a
    // healthy session, then set strikes_to_fail well above what you saw.
    uint32_t worst_run() const        { return m_worst_run; }

private:
    Config   m_cfg{};
    bool     m_have_last  = false;
    double   m_last_ref   = 0.0;
    double   m_ref_motion = 0.0;
    double   m_worst      = 0.0;
    uint32_t m_samples    = 0;
    uint32_t m_strikes    = 0;
    uint32_t m_worst_run  = 0;
};

// ------------------------------------------------------------------ reporting which rung won
//
// A cascade that does not say HOW it resolved will happily run a whole population on its fallback
// with nobody the wiser -- including you, reading your own logs. Announce on CHANGE, not every
// tick: a line that always appears carries no information.
class TierReporter {
public:
    // Returns true when this (address, tier) differs from the last reported pair, i.e. when there
    // is actually news. Caller does the logging so the wording stays in the caller's voice.
    //
    // `tier` is COPIED, not borrowed. An earlier version kept the pointer, which is safe for the
    // string literals a call site usually passes and a use-after-free for anything else -- and it
    // would only bite on the SECOND resolve, i.e. exactly when a tier change made the report matter.
    bool changed(uintptr_t address, const char* tier);

private:
    static constexpr size_t TIER_MAX = 32;
    uintptr_t m_last_address = 0;
    char      m_last_tier[TIER_MAX] = {};
    bool      m_have = false;
};

} // namespace addrcascade
