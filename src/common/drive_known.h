#pragma once
// DRIVE ADDRESSES, THE KNOWN ONES FIRST -- the rungs of one direct drive's resolution, in order, and the small
// file that remembers what a search found.
//
//   RUNG 1, THE KNOWN ADDRESSES, VERIFIED IN PLACE. No search of the module at all.
//     (a) The tables' recorded RVAs, when the module IS the build they were measured on (its PE timestamp and
//         image size; title_build_identity.h).
//     (m) Otherwise what this same process already resolved in this very module, where it has: a drive is
//         installed again at every level, and what was found in a module -- and what was NOT found in it -- does
//         not change while that module stays loaded. Every found line is verified where it stands; a line that
//         was not found is not searched for again. This one alone may repeat a result that is incomplete, or a
//         refusal: it adds nothing to what the drive came to, and takes nothing away. In memory only. Its owner
//         does not hand over a result that is not whole until the drive has been searched for afresh
//         kFreshSearches more times in that module (RepeatRemembered): one search can miss for a reason that was
//         true only then.
//     (b) Otherwise the RVAs an earlier search found in exactly this build, read back from the address cache.
//         The cache is asked only at this point: while (a) or (m) holds, its file is never opened.
//     Every signature's bytes must stand at its known address, and the drive's own plan makes every proof on
//     those addresses that it makes on searched ones (drive_resolve.h has what that is, and why leaving out the
//     count of matches is sound). A saved address is moreover taken only under the rule the search placed it by.
//     ONE line that fails, and the whole drive goes to rung 2: the two are never mixed.
//   RUNG 2, THE SEARCH (drive_resolve.h), as if nothing were known.
//   LEARN. When the search resolved the whole drive and the answer is not simply "the recorded addresses, on the
//     recorded build", the addresses are worth remembering: the caller saves them, and the next launch on this
//     build starts at rung 1 (b).
//
// THE ADDRESS CACHE is a hint, never a setting and never a shortcut past validation. An entry is one drive's
// addresses for one module build (the stamp: PE TimeDateStamp and SizeOfImage, the pair a debugger matches a PDB
// by), one version of that drive's tables (the plan hash: every name, recorded RVA and pattern the drive asks
// for) and one version of the search's rules (drive_resolve::kRulesVersion). A patched game has another stamp, an
// updated mod another plan or other rules, so each of them makes an entry dead by construction. With every
// address the entry holds HOW the search placed it, and the address is verified under that same rule (a row found
// at its recorded offset is taken only there, a signature found among several only at its recorded address), so
// a saved address is accepted where the search that saved it would take it again. An entry that does not verify
// costs a few comparisons and falls through to the search. Deleting the file is a cold start, nothing more.
//
// Each entry ends in a checksum over all of it, with a constant salt. That guards against STALE AND DAMAGED
// files: one cut short, one a tool rewrote, one a hand changed in passing. It does not stop a person who edits
// an entry on purpose and computes the checksum again, and nothing here pretends it does. What stands between
// such a file and the game is the verification: every address must hold its row's bytes, in its function, with
// every proof of the drive's plan made on it.
//
// This file is the engine-free part: the rungs, and the cache's text. It reads and writes no file (the DLL's side
// does, src/dll/drive_cascade.cpp; the tests and the offline check use the same text functions).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "drive_resolve.h"
#include "runtime_types.h"

namespace drive_known
{
using drive_resolve::Image;
using drive_resolve::Report;
using drive_resolve::Resolver;

// ------------------------------------------------------------------------------------------------- the cache
inline constexpr uint32_t kSchema = 2;
inline constexpr uint32_t kMaxModule = 24;
inline constexpr uint32_t kMaxId = 32;
inline constexpr uint32_t kMaxEntries = 16;   // one per drive of a module; there are nine drives

// One drive's addresses, as a search found them in one build of its module.
struct Entry
{
    char module[kMaxModule]{};   // "halo3.dll"
    char id[kMaxId]{};           // the drive, without spaces: "control-update"
    uint64_t stamp = 0;          // the module build they are of
    uint64_t plan = 0;           // the version of the drive's tables they answer (Report::plan)
    uint32_t rules = 0;          // the version of the search's rules that found them (drive_resolve::kRulesVersion)
    uint32_t count = 0;
    uint32_t rvas[drive_resolve::kMaxLines]{};   // every line's address, in the plan's order
    uint8_t placed[drive_resolve::kMaxLines]{};  // how the search placed each one (drive_resolve::Placement)
};

inline bool IsPlacement(uint8_t code)
{
    return code == 'U' || code == 'R' || code == 'O' || code == 'X' || code == 'S' || code == 'D';
}

// The checksum an entry is written with and read against: FNV-1a over every field, started from a salted basis
// so that it is this file's and not the plain hash of a line. The head of this file has what it guards against,
// and what it does not.
inline uint64_t Checksum(const Entry& entry)
{
    uint64_t h = 1469598103934665603ull ^ 0x48414C4F4D435652ull;
    const auto byte = [&h](uint8_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    const auto number = [&byte](uint64_t v) {
        for (int i = 0; i < 8; ++i)
            byte(static_cast<uint8_t>(v >> (i * 8)));
    };
    const auto text = [&byte](const char* s, size_t capacity) {
        for (size_t i = 0; i < capacity && s[i]; ++i)
            byte(static_cast<uint8_t>(s[i]));
        byte(0);
    };
    text(entry.module, sizeof(entry.module));
    text(entry.id, sizeof(entry.id));
    number(entry.stamp);
    number(entry.plan);
    number(entry.rules);
    number(entry.count);
    for (uint32_t i = 0; i < entry.count && i < drive_resolve::kMaxLines; ++i)
    {
        number(entry.rvas[i]);
        byte(entry.placed[i]);
    }
    return h;
}

struct File
{
    Entry entries[kMaxEntries]{};
    uint32_t count = 0;
};

// WHICH BUILD: the PE TimeDateStamp and SizeOfImage as one number.
inline uint64_t Stamp(const Image& image)
{
    return (static_cast<uint64_t>(image.timeDateStamp) << 32) | image.sizeOfImage;
}

inline const char* ModuleName(GameTitle title)
{
    switch (title)
    {
    case GameTitle::HaloCE: return "halo1.dll";
    case GameTitle::Halo2: return "halo2.dll";
    case GameTitle::Halo3: return "halo3.dll";
    case GameTitle::Halo3ODST: return "halo3odst.dll";
    case GameTitle::HaloReach: return "haloreach.dll";
    case GameTitle::Halo4: return "halo4.dll";
    default: return "";
    }
}

// A module or drive name as the file holds it: one word.
inline bool Word(const char* text, size_t capacity)
{
    if (!text || !*text)
        return false;
    size_t length = 0;
    for (const char* p = text; *p; ++p, ++length)
        if (*p <= ' ' || *p > '~' || *p == ',')
            return false;
    return length < capacity;
}

// A whole resolution as an entry. False when it is not one that may be saved: a line missing, a name that is not
// a word, or a report that is not a SEARCH's (only a search says how each line was placed; lines that were
// verified at known addresses say nothing new).
inline bool MakeEntry(Entry& entry, const char* module, const char* id, uint64_t stamp, const Report& report)
{
    entry = Entry{};
    if (!report.Complete() || !stamp || !Word(module, kMaxModule) || !Word(id, kMaxId))
        return false;
    std::snprintf(entry.module, sizeof(entry.module), "%s", module);
    std::snprintf(entry.id, sizeof(entry.id), "%s", id);
    entry.stamp = stamp;
    entry.plan = report.plan;
    entry.rules = drive_resolve::kRulesVersion;
    entry.count = report.count;
    for (uint32_t i = 0; i < report.count; ++i)
    {
        entry.rvas[i] = report.lines[i].found;
        entry.placed[i] = static_cast<uint8_t>(drive_resolve::PlacementOf(report.lines[i]));
        if (!IsPlacement(entry.placed[i]))
        {
            entry = Entry{};
            return false;
        }
    }
    return true;
}

// The entry held for a drive of a module, whatever build it is of, or null.
inline const Entry* Find(const File& file, const char* module, const char* id)
{
    for (uint32_t i = 0; i < file.count; ++i)
        if (!std::strcmp(file.entries[i].module, module) && !std::strcmp(file.entries[i].id, id))
            return &file.entries[i];
    return nullptr;
}

inline constexpr char kOtherBuild[] = "they are of another build of this game DLL";
inline constexpr char kOtherRules[] = "they were found under other rules of this mod's search";

// The entry that may be tried for a drive in the module build `stamp`: held for it, of that build, and found
// under the rules this mod searches by now. Null, with `refused`, when one is held and is not that.
inline const Entry* Usable(const File& file, const char* module, const char* id, uint64_t stamp, const char*& refused)
{
    refused = nullptr;
    const Entry* const held = Find(file, module, id);
    if (!held)
        return nullptr;
    if (held->stamp != stamp)
        refused = kOtherBuild;
    else if (held->rules != drive_resolve::kRulesVersion)
        refused = kOtherRules;
    return refused ? nullptr : held;
}

// Puts a drive's addresses in, in place of whatever was held for that drive of that module (an older build's are
// of no further use). Every other entry stays -- unless the file is full. Then one entry gives way, and the new
// one is never refused: the oldest that is surely dead (one of ANOTHER BUILD of this entry's module, which is
// the build loaded now), failing that the oldest of another module, failing that the oldest of all. The file's
// order is the order in which its drives were first saved, so "oldest" is the first in it.
inline void Put(File& file, const Entry& entry)
{
    for (uint32_t i = 0; i < file.count; ++i)
        if (!std::strcmp(file.entries[i].module, entry.module) && !std::strcmp(file.entries[i].id, entry.id))
        {
            file.entries[i] = entry;
            return;
        }
    if (file.count >= kMaxEntries)
    {
        uint32_t gives = kMaxEntries, otherModule = kMaxEntries;
        for (uint32_t i = 0; i < file.count && gives == kMaxEntries; ++i)
        {
            const bool sameModule = !std::strcmp(file.entries[i].module, entry.module);
            if (sameModule && file.entries[i].stamp != entry.stamp)
                gives = i;
            else if (!sameModule && otherModule == kMaxEntries)
                otherModule = i;
        }
        if (gives == kMaxEntries)
            gives = otherModule != kMaxEntries ? otherModule : 0;
        for (uint32_t i = gives; i + 1 < file.count; ++i)
            file.entries[i] = file.entries[i + 1];
        --file.count;
    }
    file.entries[file.count++] = entry;
}

// The longest line the file holds: an entry with every line (a letter, eight hex digits and a comma each), its
// head and its checksum.
inline constexpr size_t kMaxLineText = 200 + static_cast<size_t>(drive_resolve::kMaxLines) * 10;
// Room for the head and a full file.
inline constexpr size_t kMaxFileText = 1024 + static_cast<size_t>(kMaxEntries) * kMaxLineText;

// The file's text. The length written, or 0 when `capacity` is too small (nothing useful is in `out` then).
inline size_t Format(const File& file, char* out, size_t capacity)
{
    size_t used = 0;
    bool fits = true;
    const auto put = [&](const char* format, auto... arguments) {
        if (!fits)
            return;
        const int n = std::snprintf(out + used, capacity - used, format, arguments...);
        if (n < 0 || static_cast<size_t>(n) >= capacity - used)
            fits = false;
        else
            used += static_cast<size_t>(n);
    };
    if (!out || capacity < 2)
        return 0;
    put("%s", "# Written by HaloMCCVR. This is a cache, NOT a setting: do not edit it.\r\n"
              "# It holds where the direct drives' code was found in a game DLL that is not the build the mod's own\r\n"
              "# tables were measured on, so that the next launch can check those addresses instead of searching the\r\n"
              "# whole DLL for them again. Every address is checked again before it is used.\r\n"
              "# Each entry ends in a checksum; one that no longer matches it is ignored and searched for again.\r\n"
              "# Safe to DELETE at any time: the next launch searches once and writes it again.\r\n");
    put("schema=%u\r\n", kSchema);
    for (uint32_t i = 0; i < file.count; ++i)
    {
        const Entry& e = file.entries[i];
        // entry <module> <stamp> <drive> <plan> <rules> <count> <placement><rva>,... <checksum>
        put("entry %s %016llX %s %016llX %u %u ", e.module, static_cast<unsigned long long>(e.stamp), e.id,
            static_cast<unsigned long long>(e.plan), e.rules, e.count);
        for (uint32_t k = 0; k < e.count; ++k)
            put(k ? ",%c%X" : "%c%X", IsPlacement(e.placed[k]) ? e.placed[k] : '?', e.rvas[k]);
        put(" %016llX\r\n", static_cast<unsigned long long>(Checksum(e)));
    }
    return fits ? used : 0;
}

// Reads a file's text. Only whole, well-formed entries of this schema whose checksum is their own are taken: a
// line that is cut short, was changed, has another count of addresses than it says, or follows a schema line of
// another number is skipped, and a file with no schema line holds nothing. Never fails: what cannot be read is a
// cold start.
inline void Parse(const char* text, size_t size, File& out)
{
    out = File{};
    if (!text)
        return;
    uint32_t schema = 0;
    char line[kMaxLineText];
    for (size_t at = 0; at < size;)
    {
        size_t end = at;
        while (end < size && text[end] != '\n')
            ++end;
        const bool whole = end < size;   // a last line with no newline was cut short by something
        size_t length = end - at;
        const char* const begin = text + at;
        at = end + 1;
        if (length && begin[length - 1] == '\r')
            --length;
        if (!whole || !length || length >= sizeof(line) || begin[0] == '#')
            continue;
        std::memcpy(line, begin, length);
        line[length] = 0;
        if (!std::strncmp(line, "schema=", 7))
        {
            schema = static_cast<uint32_t>(std::strtoul(line + 7, nullptr, 10));
            continue;
        }
        if (schema != kSchema || std::strncmp(line, "entry ", 6))
            continue;
        // entry <module> <stamp> <drive> <plan> <rules> <count> <placement><rva>,... <checksum>
        Entry e;
        char* cursor = line + 6;
        const auto word = [&cursor](char* into, size_t capacity) {
            size_t n = 0;
            while (*cursor && *cursor != ' ')
            {
                if (n + 1 < capacity)
                    into[n] = *cursor;
                ++n;
                ++cursor;
            }
            if (n >= capacity)
                return false;
            into[n] = 0;
            if (*cursor == ' ')
                ++cursor;
            return n != 0;
        };
        const auto number = [&cursor](int base, uint64_t& value) {
            char* after = nullptr;
            if (*cursor == '-' || *cursor == '+' || *cursor == ' ')
                return false;
            value = std::strtoull(cursor, &after, base);
            if (after == cursor)
                return false;
            cursor = after;
            return true;
        };
        uint64_t count = 0, rules = 0, checksum = 0;
        if (!word(e.module, sizeof(e.module)) || !number(16, e.stamp) || *cursor++ != ' ' ||
            !word(e.id, sizeof(e.id)) || !number(16, e.plan) || *cursor++ != ' ' || !number(10, rules) ||
            *cursor++ != ' ' || !number(10, count) || *cursor++ != ' ' || !e.stamp || rules > 0xFFFFFFFFull ||
            !count || count > drive_resolve::kMaxLines || !Word(e.module, kMaxModule) || !Word(e.id, kMaxId))
            continue;
        e.rules = static_cast<uint32_t>(rules);
        e.count = static_cast<uint32_t>(count);
        bool good = true;
        for (uint32_t k = 0; good && k < e.count; ++k)
        {
            uint64_t rva = 0;
            e.placed[k] = static_cast<uint8_t>(*cursor);
            good = IsPlacement(e.placed[k]);
            if (good)
                ++cursor;
            good = good && number(16, rva) && rva != 0 && rva <= 0xFFFFFFFFull &&
                   *cursor++ == (k + 1 == e.count ? ' ' : ',');
            e.rvas[k] = static_cast<uint32_t>(rva);
        }
        if (good && number(16, checksum) && *cursor == 0 && checksum == Checksum(e))
            Put(out, e);
    }
}

// -------------------------------------------------------------------------------------------------- the rungs
enum class Rung : uint8_t
{
    None,
    Recorded,   // rung 1 (a): verified at the tables' recorded addresses
    Saved,      // rung 1 (b): verified at the addresses saved from an earlier search of this build
    Search,     // rung 2
    Remembered, // rung 1 (m): as this process already resolved it in this very module, verified in place
};

// What one drive's resolution went through.
struct Outcome
{
    Rung rung = Rung::None;             // the rung whose result is the one in use
    bool recordedTried = false;         // rung 1 (a) was tried (and failed, when `rung` is not Recorded)
    bool rememberedTried = false;       // rung 1 (m) was tried (and did not hold, when `rung` is not Remembered)
    bool savedTried = false;            // rung 1 (b) was tried
    const char* savedRefused = nullptr; // ... and why its addresses were not used
    bool savedLooked = false;           // the cache was asked for this drive's saved addresses (its file was opened)
    bool learn = false;                 // the search resolved the whole drive, at addresses worth saving
    uint32_t scans = 0;                 // searches run, over every rung that ran
    uint32_t moduleScans = 0;           // ... of them, the searches of the whole module's code
    const char* refused = nullptr;      // the resolution is refused whole, on whichever rung: why (kTooManyLines)
    bool searchedToo = false;           // the self-test ran the search beside a rung 1 success
    bool searchAgrees = false;          // ... and it chose the same address for every line
};

using PlanFn = void (*)(Resolver& resolver, void* context);   // runs the drive's plan; its result is kept aside
using KeepFn = void (*)(void* context);                       // that result is the one in use

// A plan that asks for more lines than a report holds (drive_resolve::kMaxLines). Not a state a real drive is in:
// the largest plan fits with room, and a test holds every plan to that.
inline constexpr char kTooManyLines[] = "its plan asks for more lines than a report holds";
inline constexpr char kSavedStale[] = "this game DLL does not have that code at them";
inline constexpr char kSavedOtherPlan[] = "they were saved for another version of this drive's tables";

// HOW OFTEN A RESULT THAT IS NOT WHOLE IS SEARCHED FOR AGAIN in one module instance before it is taken for that
// module's own and only repeated. A first search can miss a line for a reason that was true only at that moment:
// bytes over an anchor changed by something this mod cannot read through (another program's patch, later taken
// away), or a page of the module that could not be read just then (a search steps over what it cannot read). A
// miss repeated from memory alone would never heal, and a required line would mean no hook for as long as the
// DLL stays loaded. A whole result needs no second look: it is verified in place at every install.
inline constexpr uint32_t kFreshSearches = 3;

// Whether what is remembered of a drive is to be repeated (verified in place, nothing searched), or the drive
// resolved afresh. `searchedAgain`: the fresh searches already made for it in this module instance.
inline bool RepeatRemembered(const Report& remembered, uint32_t searchedAgain)
{
    return remembered.Complete() || searchedAgain >= kFreshSearches;
}

// Whether a resolution came to exactly what another did: the same plan, the same lines found at the same
// addresses, the same lines not found, each with the verdict it had.
inline bool SameReport(const Report& a, const Report& b)
{
    if (a.count != b.count || a.dropped != b.dropped || a.plan != b.plan)
        return false;
    for (uint32_t i = 0; i < a.count; ++i)
        if (a.lines[i].found != b.lines[i].found || a.lines[i].verdict != b.lines[i].verdict)
            return false;
    return true;
}

// Whether two resolutions chose the same address for every line.
inline bool SameAddresses(const Report& a, const Report& b)
{
    if (a.count != b.count)
        return false;
    for (uint32_t i = 0; i < a.count; ++i)
        if (a.lines[i].found != b.lines[i].found)
            return false;
    return true;
}

// WHAT IS ALREADY KNOWN ABOUT A DRIVE'S ADDRESSES IN THIS MODULE, beyond what the tables record.
struct Earlier
{
    // What this process already resolved for this drive in this very module instance -- the whole report, found
    // lines and lines not found alike -- or null. The caller answers for it being this module instance.
    const Report* remembered = nullptr;
    // The cache's entry for this drive in exactly this build of the module (Usable): in hand, or how to get it.
    // `look` is called at most once, and only when the entry is needed -- never while rung 1 (a) holds, so on
    // the recorded build a healthy resolution does not open the cache's file at all. It returns the entry or
    // null; `refused`: one was held and may not be tried, and why.
    const Entry* saved = nullptr;
    const Entry* (*look)(void* context, const char*& refused) = nullptr;
    void* lookContext = nullptr;
};

// One drive, by the rungs. `recordedBuild`: the module is the build the drive's tables were measured on.
// `earlier`: what else is known of its addresses. `report` is the report of the rung in use.
inline Outcome Run(const Image& image, uint32_t faults, bool recordedBuild, const Earlier& earlier, PlanFn plan,
                   KeepFn keep, void* context, Report& report)
{
    using drive_resolve::Known;
    using drive_resolve::Source;
    Outcome out;
    report = Report{};
    // A PLAN THAT ASKS FOR MORE LINES THAN A REPORT HOLDS IS REFUSED, on whichever rung it shows, and nothing of
    // it is kept: the lines the report dropped were answered "not found" on that rung, and the rest can be
    // neither verified whole, remembered, saved nor said. The rungs do not go on from there.
    const auto tooLong = [&out, &report](const Report& got) {
        if (!got.dropped)
            return false;
        out.rung = Rung::None;
        out.learn = false;
        out.refused = kTooManyLines;
        report = got;
        return true;
    };
    // A forced stale save stands for a build that is not the recorded one and whose saved addresses are wrong.
    const bool stale = drive_resolve::Fault(faults, drive_resolve::kFaultSavedStale);
    if (recordedBuild && !stale)
    {
        Resolver resolver(image, faults, Known{Source::Recorded, nullptr, 0, nullptr});
        plan(resolver, context);
        if (tooLong(resolver.report()))
            return out;
        out.recordedTried = true;
        out.scans += resolver.report().scans;
        out.moduleScans += resolver.report().moduleScans;
        if (resolver.report().Complete())
        {
            keep(context);
            report = resolver.report();
            out.rung = Rung::Recorded;
        }
    }
    // WHAT THIS PROCESS ALREADY RESOLVED IN THIS VERY MODULE, verified line for line where it stands. Nothing is
    // searched. It is taken only if the plan comes to exactly what was remembered, and never under a forced
    // fault, whose results are not the module's. What it repeats may be incomplete, or a refusal: that is what
    // the drive came to in this module, and it is said again as it was.
    if (out.rung == Rung::None && earlier.remembered && !faults)
    {
        Resolver resolver(image, 0, Known{Source::Remembered, nullptr, 0, nullptr, earlier.remembered});
        plan(resolver, context);
        if (tooLong(resolver.report()))
            return out;
        out.rememberedTried = true;
        out.scans += resolver.report().scans;
        out.moduleScans += resolver.report().moduleScans;
        if (SameReport(resolver.report(), *earlier.remembered))
        {
            keep(context);
            report = resolver.report();
            out.rung = Rung::Remembered;
        }
    }
    // THE SAVED ADDRESSES ARE ASKED FOR ONLY NOW, when neither has settled it: asking is opening a file.
    const Entry* saved = nullptr;
    if (out.rung == Rung::None)
    {
        saved = earlier.saved;
        if (!saved && earlier.look && !stale)
        {
            out.savedLooked = true;
            saved = earlier.look(earlier.lookContext, out.savedRefused);
        }
        // Addresses found under other rules are not tried at all: what those rules accepted is not known here.
        if (saved && saved->rules != drive_resolve::kRulesVersion)
        {
            out.savedRefused = kOtherRules;
            saved = nullptr;
        }
    }
    if (out.rung == Rung::None && (saved || stale))
    {
        Resolver resolver(image, faults, Known{Source::Saved, saved ? saved->rvas : nullptr, saved ? saved->count : 0,
                                               saved ? saved->placed : nullptr});
        plan(resolver, context);
        if (tooLong(resolver.report()))
            return out;
        out.savedTried = true;
        out.savedRefused = nullptr;
        out.scans += resolver.report().scans;
        out.moduleScans += resolver.report().moduleScans;
        const Report& got = resolver.report();
        if (!got.Complete())
        {
            // Which line failed first? One the entry holds an address for: that address did not verify. One past
            // the entry's last: the entry is of another version of the tables, with fewer lines than are asked
            // for now (a player build's, read by a dev build).
            uint32_t first = 0;
            while (first < got.count && drive_resolve::Found(got.lines[first].verdict))
                ++first;
            out.savedRefused = saved && first >= saved->count ? kSavedOtherPlan : kSavedStale;
        }
        else if (!saved || got.plan != saved->plan || got.count != saved->count)
        {
            out.savedRefused = kSavedOtherPlan;
        }
        else
        {
            // What follows from the verified addresses must be what followed from them when they were saved,
            // and each line must be of the kind the entry says it was placed as. (A line verified at its saved
            // address has had its placement applied where it was verified.)
            for (uint32_t i = 0; i < got.count && !out.savedRefused; ++i)
            {
                const drive_resolve::Line& line = got.lines[i];
                if (line.found != saved->rvas[i] ||
                    (line.how != drive_resolve::How::Known &&
                     static_cast<uint8_t>(drive_resolve::PlacementOf(line)) != saved->placed[i]))
                    out.savedRefused = kSavedStale;
            }
        }
        if (!out.savedRefused)
        {
            keep(context);
            report = got;
            out.rung = Rung::Saved;
        }
    }
    if (out.rung == Rung::None)
    {
        Resolver resolver(image, faults);
        plan(resolver, context);
        if (tooLong(resolver.report()))
            return out;
        keep(context);
        report = resolver.report();
        out.scans += report.scans;
        out.moduleScans += report.moduleScans;
        out.rung = Rung::Search;
        // Worth remembering unless it is simply the recorded addresses on the recorded build, which rung 1 (a)
        // has without a file.
        bool atRecorded = true;
        for (uint32_t i = 0; i < report.count; ++i)
            atRecorded = atRecorded && report.lines[i].found == report.lines[i].recorded;
        out.learn = report.Complete() && !(recordedBuild && atRecorded);
    }
    else if (drive_resolve::Fault(faults, drive_resolve::kFaultSearchToo))
    {
        // THE SELF-TEST: the search as well, beside the rung 1 result that stays in use.
        Resolver resolver(image, faults);
        plan(resolver, context);
        out.scans += resolver.report().scans;
        out.moduleScans += resolver.report().moduleScans;
        out.searchedToo = true;
        out.searchAgrees = SameAddresses(report, resolver.report());
    }
    return out;
}

// The same with the cache's entry in hand, or with none (null).
inline Outcome Run(const Image& image, uint32_t faults, bool recordedBuild, const Entry* saved, PlanFn plan, KeepFn keep,
                   void* context, Report& report)
{
    Earlier earlier;
    earlier.saved = saved;
    return Run(image, faults, recordedBuild, earlier, plan, keep, context, report);
}

// The same for a plan that is a callable: Found (Resolver&).
template <typename Found, typename Plan>
inline Outcome RunTyped(const Image& image, uint32_t faults, bool recordedBuild, const Earlier& earlier, Plan& plan,
                        Found& found, Report& report)
{
    struct Context
    {
        Plan* plan;
        Found aside;
        Found* found;
    } context{&plan, Found{}, &found};
    return Run(
        image, faults, recordedBuild, earlier,
        [](Resolver& resolver, void* raw) {
            auto* c = static_cast<Context*>(raw);
            c->aside = (*c->plan)(resolver);
        },
        [](void* raw) {
            auto* c = static_cast<Context*>(raw);
            *c->found = c->aside;
        },
        &context, report);
}

template <typename Found, typename Plan>
inline Outcome RunTyped(const Image& image, uint32_t faults, bool recordedBuild, const Entry* saved, Plan& plan,
                        Found& found, Report& report)
{
    Earlier earlier;
    earlier.saved = saved;
    return RunTyped(image, faults, recordedBuild, earlier, plan, found, report);
}
} // namespace drive_known
