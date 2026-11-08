#pragma once
// DRIVE ADDRESS RESOLUTION -- where a direct drive's code is in THIS build of a game DLL.
//
// Every direct drive hooks one game function and reads a handful of fields whose offsets it knows from the
// instructions that use them. The tables that describe those places (drive_sites.h) record an RVA for each: a
// measurement of one build. A recorded RVA is not where the code is after the game is patched, and hooking is
// positional -- a hook placed on a stale address installs and reports success. So nothing here trusts a recorded
// RVA to say where anything is.
//
// THE KNOWN ADDRESSES FIRST. A resolution may be handed the address every signature is already known to stand at
// (Known): the tables' recorded RVAs when the module is the very build they were measured on, or the RVAs an
// earlier search found in this same build (drive_known.h decides which, and runs the rungs). It then compares the
// bytes AT that address and searches nothing. That is a hint and never a shortcut past validation: the bytes must
// match there, and every proof the search path makes about an address -- a function start is a .pdata function
// start, an anchor lies in its function, a call lands where it must, the TLS index is the module's own -- is made
// on it all the same, by the same plan. The one thing it does not do is count the matches in the whole module.
// That count exists to tell one place from another when the address is NOT known; here the address is known for
// exactly this module (its PE timestamp and image size), and the count was made when it became known: by the
// offline check for the recorded build, by the search that produced a saved address. And the search itself takes
// a signature whose bytes stand at its recorded address even among several matches, so on the recorded build the
// two always choose the same address. If any one line fails, the whole resolution has failed and is run again by
// search; the two are never mixed.
//
// A SAVED ADDRESS IS TAKEN ONLY AS THE SEARCH THAT SAVED IT WOULD TAKE IT AGAIN. With each address the cache holds
// how the search placed it (Placement), and the verification applies that rule, never a looser one: a signature
// taken at its recorded address among several matches is accepted only there; an in-function row found at its
// recorded offset only at that offset; one found as the single match in its function only if the function,
// searched again, holds it once and not at the offset. One rule cannot be applied again without the search
// itself: that a signature saved as the module's single match, at an address other than its recorded one, has no
// OTHER match anywhere in the module. There the entry's own word stands for the count -- an entry of exactly this
// module build, these tables and these rules, with its checksum intact (drive_known.h) -- after the two checks
// that can be made in place: the bytes stand at the saved address, and they do not stand at the recorded one,
// where the search would have taken them instead.
//
// WHAT THIS PROCESS ALREADY RESOLVED IN THIS VERY MODULE is known too (Source::Remembered): the report of an
// earlier resolution of the same module instance, handed back line for line. A drive is installed again whenever
// its title's core is, and the module it resolves in has not changed in between. A line that was found is
// verified where it was found: its bytes must still stand there. A line that was NOT found is not searched for
// again: the search that did not find it searched this module (and whoever hands the report over has had the
// module searched afresh a few times before handing over one that is not whole: drive_known::RepeatRemembered).
// What follows from other lines is worked out again.
// Nothing is scanned. If the plan then comes to anything but what was remembered, the remembered report is void
// and the caller resolves afresh; so a result is only ever repeated whole, never patched.
//
// THE SEARCH. The rungs, in order:
//
//   1. BY SIGNATURE. A hooked function, and every anchor whose bytes name one place, is searched for in the
//      module's executable sections, and EVERY match is counted. Exactly one match is the address, wherever it is.
//      The recorded RVA is then only the cross-check: "as recorded", or "MOVED from +0x.. to +0x..".
//   2. THE RECORD AS THE TIE-BREAK. Several matches: the anchor is taken only if its bytes also stand at the
//      recorded RVA, and the report says so. The first of several matches is never taken.
//   3. BY ITS FUNCTION. An anchor that is short by nature (the one instruction that reads a field, with its
//      displacement bytes exact) is not searched for in the module at all. It is looked for at its recorded offset
//      from the START of the function it sits in, that start taken from the module's own .pdata for an anchor
//      already found there; failing that, a single match inside that function's extent is accepted.
//   4. NOTHING. No match, or no way to tell several apart: the anchor is not found, the drive that needs it is not
//      hooked, and one line names what failed. A wrong hook crashes the game; a missing one costs direct aim.
//
// Addresses that follow from found ones are derived, never recorded on their own: a call instruction at a fixed
// offset inside a found pattern, the start of the function a found call site is in (.pdata), the address a found
// instruction loads from. The engine's TLS index is the module's own: it is read from the PE TLS directory and
// must be the address the drive's own anchored instruction loads.
//
// This file is the engine-free part: it reads an image through a small view (bytes, executable ranges, .pdata),
// so the same code resolves a module loaded in the game, a DLL file laid out by the offline check
// (tools/drive_address_check.cpp), and the synthetic images of tests/drive_resolve_tests.cpp. It allocates
// nothing, logs nothing and has no object that needs unwinding (the DLL runs it under a structured-exception
// guard).
//
// FAULT INJECTION. Rungs 2 to 4 only run when something is already wrong, so on a healthy game none of them ever
// executes. Each can be forced (the kFault bits), to watch the rung below it work. The forcing code is compiled
// only where DRIVE_RESOLVE_FAULTS is 1 -- dev builds of the DLL, the tests and the offline check -- and is absent
// from a player build.

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef DRIVE_RESOLVE_FAULTS
#if defined(ADDRCASCADE_FAULTS) && ADDRCASCADE_FAULTS
#define DRIVE_RESOLVE_FAULTS 1
#else
#define DRIVE_RESOLVE_FAULTS 0
#endif
#endif

namespace drive_resolve
{
// THE SEARCH'S RULES, NUMBERED. Bump this whenever what the search accepts changes: which of several matches it
// takes, where it looks for an in-function row, what it refuses. Saved addresses are only as good as the rules
// that found them, so an entry written under another number is discarded (drive_known.h).
inline constexpr uint32_t kRulesVersion = 1;

// ADDR-HYGIENE: structural -- fault-injection bits and the shift a forced fault applies, not addresses of anything.
inline constexpr uint32_t kFaultSignatureMissing = 0x01;     // every signature search finds nothing
inline constexpr uint32_t kFaultSignatureAmbiguous = 0x02;   // every signature search finds one match too many
inline constexpr uint32_t kFaultRecordWrong = 0x04;          // every recorded RVA reads kFaultRecordShift too high
inline constexpr uint32_t kFaultTlsDirectory = 0x08;         // the PE TLS directory names another address
inline constexpr uint32_t kFaultHookSilent = 0x10;           // a drive's watchdog is told its hook never ran
inline constexpr uint32_t kFaultOffsetMoved = 0x20;          // no anchor stands at its recorded offset in its function
inline constexpr uint32_t kFaultSavedStale = 0x40;           // the addresses saved from an earlier launch are stale
inline constexpr uint32_t kFaultSearchToo = 0x80;            // a self-test, not a fault: the search is run as well
// The bits that change what a resolution finds. A result made under one of them is not the game's.
inline constexpr uint32_t kFaultsThatForce = kFaultSignatureMissing | kFaultSignatureAmbiguous | kFaultRecordWrong |
                                             kFaultTlsDirectory | kFaultOffsetMoved | kFaultSavedStale;
inline constexpr uint32_t kFaultRecordShift = 0x1000;

#if DRIVE_RESOLVE_FAULTS
inline bool Fault(uint32_t faults, uint32_t bit) { return (faults & bit) != 0; }
#else
inline constexpr bool Fault(uint32_t, uint32_t) { return false; }
#endif

// ------------------------------------------------------------------------------------------------- patterns
// "48 8B ?? C0": hex bytes, "??" (or "?") for any byte. The grammar of sig::Find.
inline constexpr uint32_t kMaxPattern = 256;

struct Pattern
{
    uint8_t bytes[kMaxPattern];
    uint8_t wild[kMaxPattern];
    uint32_t length;
};

// False when the text is not a pattern, is longer than kMaxPattern, or has no byte to match on.
inline bool Compile(const char* text, Pattern& out)
{
    out.length = 0;
    if (!text)
        return false;
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    bool concrete = false;
    for (const char* p = text; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        if (out.length >= kMaxPattern)
            return false;
        if (*p == '?')
        {
            out.bytes[out.length] = 0;
            out.wild[out.length] = 1;
            ++out.length;
            ++p;
            if (*p == '?')
                ++p;
            continue;
        }
        const int hi = hex(p[0]);
        const int lo = hi < 0 ? -1 : hex(p[1]);
        if (hi < 0 || lo < 0)
            return false;
        out.bytes[out.length] = static_cast<uint8_t>((hi << 4) | lo);
        out.wild[out.length] = 0;
        ++out.length;
        concrete = true;
        p += 2;
    }
    return out.length != 0 && concrete;
}

// ---------------------------------------------------------------------------------------------------- image
struct Range
{
    uint32_t rva = 0;
    uint32_t size = 0;
};

// An x64 RUNTIME_FUNCTION as the module's exception directory (.pdata) holds it.
struct RuntimeFunction
{
    uint32_t begin;
    uint32_t end;
    uint32_t unwind;
};

inline constexpr uint32_t kMaxExecutableRanges = 8;

// A module's image: its bytes at their RVAs, its executable sections, its .pdata. `original` and `correct` make
// every read the game's OWN bytes where this mod has an enabled hook (sig::CopyUnhooked,
// sig::CorrectLiveCountForHooks): a proof must not depend on whether it runs before or after another lane hooked
// the same function. Both null: the bytes as they stand (a DLL file laid out by the offline check, a test image).
struct Image
{
    const uint8_t* base = nullptr;
    uint32_t size = 0;
    Range executable[kMaxExecutableRanges]{};
    uint32_t executableCount = 0;
    const RuntimeFunction* functions = nullptr;   // sorted by begin, as the loader requires
    uint32_t functionCount = 0;
    // Where the PE TLS directory says the module's TLS index lives (0: not read). Filled by the owner of the image.
    uint32_t tlsIndexRva = 0;
    // WHICH BUILD THIS IS, from the PE headers: the pair a set of known addresses is recorded or saved for.
    uint32_t timeDateStamp = 0;
    uint32_t sizeOfImage = 0;
    // Whether [address, address + size) can be read at all. A loaded module may hold pages that cannot (the
    // owner of the image asks the system); null: everything inside the image can. Every read outside the
    // executable runs asks it first, so a page that cannot be read costs the one line that needed it, not the
    // resolution.
    bool (*readable)(uintptr_t address, size_t size) = nullptr;
    uint32_t (*original)(uintptr_t address, void* out, size_t size) = nullptr;
    void (*correct)(uintptr_t begin, size_t size, const uint8_t* bytes, const uint8_t* wild, size_t n, uintptr_t& first,
                    uint32_t& count) = nullptr;
};

// Reads the executable sections and the exception directory out of a mapped PE32+ image's own headers. `why`
// names what was refused. Every offset is bounds-checked against `size` before it is read. `readable`: the
// owner's word on whether a range of a loaded module can be read at all (Image::readable; the image keeps it).
// The function table is asked about BEFORE any of it is read: one that cannot be read is no table, and the
// rest of the image is read all the same.
inline bool ReadImage(const uint8_t* base, size_t size, Image& out, const char*& why,
                      bool (*readable)(uintptr_t address, size_t size) = nullptr)
{
    out = Image{};
    out.readable = readable;
    why = nullptr;
    const auto u16 = [base](size_t at) {
        uint16_t v = 0;
        std::memcpy(&v, base + at, sizeof(v));
        return v;
    };
    const auto u32 = [base](size_t at) {
        uint32_t v = 0;
        std::memcpy(&v, base + at, sizeof(v));
        return v;
    };
    if (!base || size < 0x400 || size > 0xFFFFF000u)
    {
        why = "the image is missing or its size is not a module's";
        return false;
    }
    if (u16(0) != 0x5A4D)
    {
        why = "no DOS header";
        return false;
    }
    const size_t pe = u32(0x3C);
    if (pe > size - 0x200 || u32(pe) != 0x00004550u)
    {
        why = "no PE header";
        return false;
    }
    const uint32_t sections = u16(pe + 6);
    const size_t optional = pe + 24;
    const size_t optionalSize = u16(pe + 20);
    if (u16(pe + 4) != 0x8664 || u16(optional) != 0x20B || optionalSize < 112 + 16 * 8)
    {
        why = "not an x64 PE32+ image";
        return false;
    }
    const uint32_t directories = u32(optional + 108);
    const size_t table = optional + optionalSize;
    if (!sections || sections > 96 || directories < 10 || table + static_cast<size_t>(sections) * 40 > size)
    {
        why = "the section table does not fit the image";
        return false;
    }
    out.base = base;
    out.size = static_cast<uint32_t>(size);
    out.timeDateStamp = u32(pe + 8);
    out.sizeOfImage = u32(optional + 56);
    for (uint32_t i = 0; i < sections; ++i)
    {
        const size_t at = table + static_cast<size_t>(i) * 40;
        const uint32_t virtualSize = u32(at + 8), rva = u32(at + 12), characteristics = u32(at + 36);
        if (!(characteristics & 0x20000000u) || !virtualSize)   // IMAGE_SCN_MEM_EXECUTE
            continue;
        if (rva >= size || virtualSize > size - rva)
        {
            why = "an executable section lies outside the image";
            return false;
        }
        if (out.executableCount >= kMaxExecutableRanges)
        {
            why = "more executable sections than this reader holds";
            return false;
        }
        out.executable[out.executableCount++] = Range{rva, virtualSize};
    }
    if (!out.executableCount)
    {
        why = "no executable section";
        return false;
    }
    const uint32_t pdataRva = u32(optional + 112 + 3 * 8), pdataSize = u32(optional + 112 + 3 * 8 + 4);
    if (pdataRva && pdataSize >= sizeof(RuntimeFunction))
    {
        if ((pdataRva & 3u) || pdataRva >= size || pdataSize > size - pdataRva)
        {
            why = "the exception directory lies outside the image";
            return false;
        }
        out.functions = reinterpret_cast<const RuntimeFunction*>(base + pdataRva);
        out.functionCount = pdataSize / static_cast<uint32_t>(sizeof(RuntimeFunction));
        // A table that cannot be read is no table: nothing that needs a function's start or extent resolves,
        // and the drives that need one are not hooked.
        if (readable && !readable(reinterpret_cast<uintptr_t>(out.functions),
                                  static_cast<size_t>(out.functionCount) * sizeof(RuntimeFunction)))
        {
            out.functions = nullptr;
            out.functionCount = 0;
        }
        // A zeroed tail is padding, and would break the search below.
        while (out.functionCount && !out.functions[out.functionCount - 1].begin)
            --out.functionCount;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------- reads
inline bool InImage(const Image& im, uint32_t rva, uint32_t length)
{
    return im.base && length && rva < im.size && length <= im.size - rva;
}

inline bool InExecutable(const Image& im, uint32_t rva, uint32_t length)
{
    for (uint32_t i = 0; i < im.executableCount; ++i)
        if (length && rva >= im.executable[i].rva && rva - im.executable[i].rva < im.executable[i].size &&
            length <= im.executable[i].size - (rva - im.executable[i].rva))
            return true;
    return false;
}

// [rva, rva + length) lies in the image and can be read.
inline bool Readable(const Image& im, uint32_t rva, uint32_t length)
{
    return InImage(im, rva, length) &&
           (!im.readable || im.readable(reinterpret_cast<uintptr_t>(im.base + rva), length));
}

// The game's own bytes at [rva, rva + length), length <= kMaxPattern.
inline bool ReadOriginal(const Image& im, uint32_t rva, uint8_t* out, uint32_t length)
{
    if (!Readable(im, rva, length) || length > kMaxPattern)
        return false;
    if (im.original)
        (void)im.original(reinterpret_cast<uintptr_t>(im.base + rva), out, length);
    else
        std::memcpy(out, im.base + rva, length);
    return true;
}

inline bool MatchesAt(const Image& im, const Pattern& p, uint32_t rva)
{
    uint8_t bytes[kMaxPattern];
    if (!p.length || !ReadOriginal(im, rva, bytes, p.length))
        return false;
    for (uint32_t i = 0; i < p.length; ++i)
        if (!p.wild[i] && bytes[i] != p.bytes[i])
            return false;
    return true;
}

// How many places a pattern matches, counted to kCountCap (enough to say "several"), and the lowest of them.
inline constexpr uint32_t kCountCap = 9;

struct Hits
{
    uint32_t first = 0;
    uint32_t count = 0;
};

// Every match that lies wholly inside [begin, end). Candidate offsets are those where the pattern's first
// concrete byte stands; each is then compared in full.
inline Hits CountIn(const Image& im, const Pattern& p, uint32_t begin, uint32_t end)
{
    Hits hits;
    if (!im.base || !p.length || begin >= end || end > im.size || end - begin < p.length)
        return hits;
    uint32_t anchor = 0;
    while (anchor < p.length && p.wild[anchor])
        ++anchor;
    if (anchor == p.length)
        return hits;
    const uint8_t anchorByte = p.bytes[anchor];
    const uint8_t* const data = im.base;
    const size_t last = static_cast<size_t>(end) - p.length;   // the last offset a match may start at
    for (size_t at = begin; at <= last;)
    {
        const void* const hit = std::memchr(data + at + anchor, anchorByte, last - at + 1);
        if (!hit)
            break;
        at = static_cast<size_t>(static_cast<const uint8_t*>(hit) - data) - anchor;
        uint32_t i = 0;
        for (; i < p.length; ++i)
            if (!p.wild[i] && data[at + i] != p.bytes[i])
                break;
        if (i == p.length)
        {
            if (!hits.count)
                hits.first = static_cast<uint32_t>(at);
            if (++hits.count >= kCountCap)
                return hits;
        }
        ++at;
    }
    // The count above is of the bytes as they stand. Where this mod has an enabled hook those are its jump: make
    // it the count over the game's own bytes (exact while the live count is below two).
    if (im.correct && hits.count < 2)
    {
        uintptr_t first = hits.count ? reinterpret_cast<uintptr_t>(data + hits.first) : 0;
        uint32_t count = hits.count;
        im.correct(reinterpret_cast<uintptr_t>(data + begin), static_cast<size_t>(end - begin), p.bytes, p.wild, p.length,
                   first, count);
        hits.count = count;
        hits.first = (count && first) ? static_cast<uint32_t>(first - reinterpret_cast<uintptr_t>(data)) : 0;
    }
    return hits;
}

// Every match inside [begin, end) that lies wholly in one of the module's executable runs: a function's extent
// is searched only where the module can be read.
inline Hits CountInExecutable(const Image& im, const Pattern& p, uint32_t begin, uint32_t end)
{
    Hits total;
    for (uint32_t i = 0; i < im.executableCount && total.count < kCountCap; ++i)
    {
        const uint32_t runBegin = im.executable[i].rva, runEnd = runBegin + im.executable[i].size;
        const uint32_t from = begin > runBegin ? begin : runBegin, to = end < runEnd ? end : runEnd;
        if (from >= to)
            continue;
        const Hits part = CountIn(im, p, from, to);
        if (part.count && (!total.count || part.first < total.first))
            total.first = part.first;
        total.count += part.count;
    }
    if (total.count > kCountCap)
        total.count = kCountCap;
    return total;
}

// Every match in the module's executable sections.
inline Hits CountExecutable(const Image& im, const Pattern& p)
{
    Hits total;
    for (uint32_t i = 0; i < im.executableCount && total.count < kCountCap; ++i)
    {
        const Hits part = CountIn(im, p, im.executable[i].rva, im.executable[i].rva + im.executable[i].size);
        if (part.count && (!total.count || part.first < total.first))
            total.first = part.first;
        total.count += part.count;
    }
    if (total.count > kCountCap)
        total.count = kCountCap;
    return total;
}

// --------------------------------------------------------------------------------------------------- .pdata
// The .pdata entry that holds `rva`, or null (a leaf function has none).
inline const RuntimeFunction* EntryAt(const Image& im, uint32_t rva)
{
    uint32_t lo = 0, hi = im.functionCount;
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2;
        const RuntimeFunction& f = im.functions[mid];
        if (rva < f.begin)
            hi = mid;
        else if (rva >= f.end)
            lo = mid + 1;
        else
            return &f;
    }
    return nullptr;
}

// The entry a function's fragment belongs to. MSVC describes one function with several entries where its
// unwind state changes (UNW_FLAG_CHAININFO: the fragment's unwind info ends with its parent's entry), and an
// entry may also point at another entry instead of unwind info (the low bit of its unwind address).
inline bool PrimaryEntry(const Image& im, RuntimeFunction entry, RuntimeFunction& primary)
{
    for (int hop = 0; hop < 16; ++hop)
    {
        if (entry.unwind & 1u)
        {
            const uint32_t at = entry.unwind & ~1u;
            if (!Readable(im, at, sizeof(RuntimeFunction)))
                return false;
            std::memcpy(&entry, im.base + at, sizeof(entry));
            continue;
        }
        if (!Readable(im, entry.unwind, 4))
            return false;
        const uint8_t* info = im.base + entry.unwind;
        if (!((info[0] >> 3) & 4u))   // UNW_FLAG_CHAININFO
        {
            primary = entry;
            return entry.begin < entry.end;
        }
        const uint32_t codes = (static_cast<uint32_t>(info[2]) + 1u) & ~1u;
        const uint32_t at = entry.unwind + 4u + codes * 2u;
        if (!Readable(im, at, sizeof(RuntimeFunction)))
            return false;
        std::memcpy(&entry, im.base + at, sizeof(entry));
    }
    return false;
}

// Where the function that holds `rva` starts, or 0 (no .pdata entry, or a chain that does not resolve).
inline uint32_t FunctionStartOf(const Image& im, uint32_t rva)
{
    const RuntimeFunction* at = EntryAt(im, rva);
    RuntimeFunction primary{};
    if (!at || !PrimaryEntry(im, *at, primary))
        return 0;
    // The start must itself be the start of a table entry.
    const RuntimeFunction* head = EntryAt(im, primary.begin);
    return head && head->begin == primary.begin ? primary.begin : 0;
}

// The end of the function that starts at `start`: the end of its own entry, extended over the entries that
// follow it without a gap and chain back to it.
inline uint32_t FunctionEndOf(const Image& im, uint32_t start)
{
    const RuntimeFunction* head = EntryAt(im, start);
    if (!head || head->begin != start)
        return 0;
    uint32_t end = head->end;
    const RuntimeFunction* const tableEnd = im.functions + im.functionCount;
    for (const RuntimeFunction* next = head + 1; next < tableEnd && next->begin == end; ++next)
    {
        RuntimeFunction primary{};
        if (!PrimaryEntry(im, *next, primary) || primary.begin != start || next->end <= next->begin)
            break;
        end = next->end;
    }
    return end;
}

// ---------------------------------------------------------------------------------------------------- report
enum class Verdict : uint8_t
{
    None,
    AsRecorded,            // found, and where the table says it is
    Moved,                 // found somewhere else: the table is of another build
    RecordAmongSeveral,    // several matches; the one at the recorded address is taken
    Missing,               // no match
    Ambiguous,             // several matches, and none at the recorded address
    NoFunction,            // the function it sits in was not found
    Refused,               // found, but a proof about it failed
};

enum class How : uint8_t
{
    None,
    Signature,         // by its bytes, anywhere in the executable sections
    FunctionOffset,    // at its recorded offset from the start of its function
    FunctionExtent,    // the one match inside its function
    FunctionStart,     // the start of the function that holds another anchor (.pdata)
    Derived,           // computed from a found address
    TlsDirectory,      // read from the PE TLS directory
    Known,             // its bytes compared at the address it is known to stand at; nothing searched
    Proof,             // not an address: a proof about found addresses, which the drive needs to hold
};

inline bool Found(Verdict v)
{
    return v == Verdict::AsRecorded || v == Verdict::Moved || v == Verdict::RecordAmongSeveral;
}

inline const char* VerdictText(Verdict v)
{
    switch (v)
    {
    case Verdict::AsRecorded: return "as recorded";
    case Verdict::Moved: return "MOVED";
    case Verdict::RecordAmongSeveral: return "several matches, taken at the recorded address";
    case Verdict::Missing: return "NOT FOUND";
    case Verdict::Ambiguous: return "AMBIGUOUS";
    case Verdict::NoFunction: return "its function was not found";
    case Verdict::Refused: return "REFUSED";
    default: return "-";
    }
}

// What a line says when its signature is one that is taken at its recorded offset or not at all.
inline constexpr char kOffsetOnly[] =
    "it is not at its recorded offset in its function, and it is too short to be searched for there";

// How many bytes a signature has (0: it does not compile).
inline uint32_t PatternLength(const char* text)
{
    Pattern p;
    return Compile(text, p) ? p.length : 0;
}

inline const char* HowText(How h)
{
    switch (h)
    {
    case How::Signature: return "signature";
    case How::FunctionOffset: return "offset in its function";
    case How::FunctionExtent: return "one match in its function";
    case How::FunctionStart: return "start of its function";
    case How::Derived: return "derived";
    case How::TlsDirectory: return "TLS directory";
    case How::Known: return "verified at its known address";
    case How::Proof: return "proved";
    default: return "-";
    }
}

struct Line
{
    const char* name = "";
    uint32_t recorded = 0;   // as the resolver read the table (a forced fault shifts it)
    uint32_t found = 0;
    uint32_t matches = 0;    // signature matches counted (to kCountCap), where a search ran
    Verdict verdict = Verdict::None;
    How how = How::None;
    const char* note = nullptr;   // the proof that failed, for Refused
};

inline constexpr uint32_t kMaxLines = 112;

// HOW THE SEARCH PLACED A LINE: what it established about the address when it found it. Saved with the address,
// so that verifying a saved address applies the rule that placed it and no weaker one. The values are the
// letters the address cache's file holds.
enum class Placement : uint8_t
{
    None = 0,
    Unique = 'U',    // by signature: the single match in the module's code
    Record = 'R',    // by signature: one of several matches, taken because it stands at the recorded address
    Offset = 'O',    // in its function: at its recorded offset from the function's start
    Extent = 'X',    // in its function: not at that offset, the single match inside the function
    Start = 'S',     // the start of the function that holds another line
    Follows = 'D',   // not searched for: it follows from other lines (a derived address, the TLS index, a proof)
};

// None for a line that was not found, or that no search placed (one verified at a known address).
inline Placement PlacementOf(const Line& line)
{
    if (!Found(line.verdict))
        return Placement::None;
    switch (line.how)
    {
    case How::Signature: return line.verdict == Verdict::RecordAmongSeveral ? Placement::Record : Placement::Unique;
    case How::FunctionOffset: return Placement::Offset;
    case How::FunctionExtent: return Placement::Extent;
    case How::FunctionStart: return Placement::Start;
    case How::Derived:
    case How::TlsDirectory:
    case How::Proof: return Placement::Follows;
    default: return Placement::None;
    }
}

struct Report
{
    Line lines[kMaxLines]{};
    uint32_t count = 0;
    // Lines the plan asked for beyond kMaxLines. None of them is looked for and none is found, on any rung; and
    // a resolution that has one is refused whole (drive_known::Run): what a report cannot hold can be neither
    // verified, remembered, saved nor said.
    uint32_t dropped = 0;
    uint32_t scans = 0;     // searches run: of the module's code for a signature, or of one function's extent
    uint32_t moduleScans = 0;   // ... of them, the searches of the whole module's code: the ones that cost
    // WHAT WAS ASKED: a hash of every line's name, recorded RVA and pattern, in order. Two resolutions of the same
    // tables that took the same path have the same one, whatever they found.
    uint64_t plan = 1469598103934665603ull;

    // Every line was found: the whole drive resolved.
    bool Complete() const { return count != 0 && dropped == 0 && NotFound() == 0; }

    uint32_t Count(Verdict v) const
    {
        uint32_t n = 0;
        for (uint32_t i = 0; i < count; ++i)
            n += lines[i].verdict == v ? 1u : 0u;
        return n;
    }
    uint32_t NotFound() const
    {
        uint32_t n = 0;
        for (uint32_t i = 0; i < count; ++i)
            n += Found(lines[i].verdict) ? 0u : 1u;
        return n;
    }
    // One number for "what was found, and how": any change in any line changes it.
    uint64_t Fingerprint() const
    {
        uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](uint32_t v) {
            for (int i = 0; i < 4; ++i)
            {
                h ^= (v >> (i * 8)) & 0xFFu;
                h *= 1099511628211ull;
            }
        };
        for (uint32_t i = 0; i < count; ++i)
        {
            mix(lines[i].found);
            mix(static_cast<uint32_t>(lines[i].verdict) | (static_cast<uint32_t>(lines[i].how) << 8));
        }
        return h;
    }
};

// --------------------------------------------------------------------------------------------- known addresses
// Where a resolution's signatures are already known to stand (the file's head has why this is sound).
enum class Source : uint8_t
{
    Search,     // nowhere: every signature is searched for, and every match counted
    Recorded,   // at the tables' recorded RVAs: the module is the build they were measured on
    Saved,      // at the RVAs an earlier search found in this same build
    Remembered, // as an earlier resolution of this very module instance left every line, found or not
};

struct Known
{
    Source source = Source::Search;
    const uint32_t* saved = nullptr;   // Saved: the RVA of every line, in the order the plan stores them
    uint32_t savedCount = 0;
    const uint8_t* placed = nullptr;   // Saved: how the search placed each of them (Placement), in the same order
    const Report* remembered = nullptr;   // Remembered: the report of that earlier resolution
};

// -------------------------------------------------------------------------------------------------- resolver
class Resolver
{
public:
    Resolver(const Image& image, uint32_t faults, Known known = {}) : im_(image), faults_(faults), known_(known) {}

    const Image& image() const { return im_; }
    const Report& report() const { return report_; }
    uint32_t faults() const { return faults_; }

    // What the table records, as this resolution reads it.
    uint32_t Recorded(uint32_t rva) const
    {
        return rva && Fault(faults_, kFaultRecordWrong) ? rva + kFaultRecordShift : rva;
    }

    // RUNG 1 and 2: by signature anywhere in the executable sections. The RVA found, or 0.
    uint32_t Unique(const char* name, uint32_t recordedRva, const char* pattern)
    {
        Ask(1, name, recordedRva, 0, pattern);
        if (Over())
            return 0;
        const uint32_t recorded = Recorded(recordedRva);
        Line line;
        line.name = name;
        line.recorded = recorded;
        line.how = How::Signature;
        Pattern p;
        if (!Compile(pattern, p))
        {
            line.verdict = Verdict::Refused;
            line.note = "its pattern does not compile";
            return Add(line);
        }
        if (known_.source == Source::Remembered)
            return Add(Again(line, p, false, 0, 0));
        if (known_.source != Source::Search)
        {
            // THE KNOWN ADDRESS: the bytes there are compared, and nothing is searched. A forced "not found" or
            // "ambiguous" is a fault of the search; it fails this rung so that the search runs and shows it.
            line.how = How::Known;
            const uint32_t at = KnownAt(recorded);
            // A SAVED address is taken only as the search that saved it would take it again. Saved as one of
            // several matches: only at the recorded address. Saved as the single match: at the recorded address,
            // or elsewhere only while these bytes do NOT also stand at the recorded address -- there the search
            // would have taken that one. (That no third place holds them is the entry's word: the file's head.)
            bool placedAsSearched = known_.source == Source::Recorded;
            if (known_.source == Source::Saved)
            {
                const Placement placed = SavedPlacement();
                if (placed == Placement::Record)
                    placedAsSearched = at == recorded;
                else if (placed == Placement::Unique)
                    placedAsSearched =
                        at == recorded || !(InExecutable(im_, recorded, p.length) && MatchesAt(im_, p, recorded));
            }
            if (at && placedAsSearched && !Fault(faults_, kFaultSignatureMissing | kFaultSignatureAmbiguous) &&
                InExecutable(im_, at, p.length) && MatchesAt(im_, p, at))
            {
                line.found = at;
                line.verdict = at == recorded ? Verdict::AsRecorded : Verdict::Moved;
            }
            else
            {
                line.verdict = Verdict::Missing;
            }
            return Add(line);
        }
        ++report_.scans;
        ++report_.moduleScans;
        Hits hits = CountExecutable(im_, p);
        if (Fault(faults_, kFaultSignatureMissing))
            hits = Hits{};
        else if (Fault(faults_, kFaultSignatureAmbiguous) && hits.count)
            ++hits.count;
        line.matches = hits.count;
        if (hits.count == 1)
        {
            line.found = hits.first;
            line.verdict = hits.first == recorded ? Verdict::AsRecorded : Verdict::Moved;
        }
        else if (!hits.count)
        {
            line.verdict = Verdict::Missing;
        }
        else if (InExecutable(im_, recorded, p.length) && MatchesAt(im_, p, recorded))
        {
            line.found = recorded;
            line.verdict = Verdict::RecordAmongSeveral;
        }
        else
        {
            line.verdict = Verdict::Ambiguous;
        }
        return Add(line);
    }

    // RUNG 3: an anchor in the function that holds `viaFound` (an anchor already found). `functionRva`: where that
    // function starts in the build the table was measured on.
    // `offsetOnly`: the signature is too short to be told from another instruction by a search of the function
    // (the four bytes of one lea, the seven of one mov): it is taken at its recorded offset or not at all. What
    // such a line proves is a struct offset the drive reads through, and a wrong one would be believed.
    uint32_t InFunction(const char* name, uint32_t recordedRva, uint32_t functionRva, uint32_t viaFound,
                        const char* pattern, bool offsetOnly = false)
    {
        Ask(offsetOnly ? 7 : 2, name, recordedRva, functionRva, pattern);
        if (Over())
            return 0;
        const uint32_t recorded = Recorded(recordedRva), functionRecorded = Recorded(functionRva);
        Line line;
        line.name = name;
        line.recorded = recorded;
        line.how = How::FunctionOffset;
        Pattern p;
        if (!Compile(pattern, p))
        {
            line.verdict = Verdict::Refused;
            line.note = "its pattern does not compile";
            return Add(line);
        }
        const uint32_t start = viaFound ? FunctionStartOf(im_, viaFound) : 0;
        const uint32_t end = start ? FunctionEndOf(im_, start) : 0;
        if (known_.source == Source::Remembered)
            return Add(Again(line, p, true, start, end));
        if (!start || !end || recorded < functionRecorded)
        {
            line.verdict = Verdict::NoFunction;
            return Add(line);
        }
        if (known_.source == Source::Saved)
        {
            // THE SAVED ADDRESS, taken as the search that saved it would take it again and no more loosely. A row
            // saved "at its recorded offset" is taken only there. One saved as "the single match in its function"
            // is taken only if these bytes do not stand at the offset (where the search looks first) and the
            // function, searched again, holds them exactly once: a function is small, so that rule is applied
            // whole. A row too short to be searched for is never taken anywhere but at its offset.
            line.how = How::Known;
            const uint32_t at = KnownAt(recorded);
            const uint32_t offset = start + (recorded - functionRecorded);
            const Placement placed = SavedPlacement();
            bool placedAsSearched = false;
            if (placed == Placement::Offset)
            {
                placedAsSearched = at == offset;
            }
            else if (placed == Placement::Extent && !offsetOnly &&
                     !(offset >= start && offset < end && p.length <= end - offset &&
                       InExecutable(im_, offset, p.length) && MatchesAt(im_, p, offset)))
            {
                ++report_.scans;
                const Hits hits = CountInExecutable(im_, p, start, end);
                line.matches = hits.count;
                placedAsSearched = hits.count == 1 && hits.first == at;
            }
            if (at && placedAsSearched && !Fault(faults_, kFaultOffsetMoved) && at >= start && at < end &&
                p.length <= end - at && InExecutable(im_, at, p.length) && MatchesAt(im_, p, at))
            {
                line.found = at;
                line.verdict = at == recorded ? Verdict::AsRecorded : Verdict::Moved;
            }
            else
            {
                line.verdict = Verdict::Missing;
            }
            return Add(line);
        }
        // The offset must land inside this function, not merely in some code.
        const uint32_t expected = start + (recorded - functionRecorded);
        if (!Fault(faults_, kFaultOffsetMoved) && expected >= start && expected < end && p.length <= end - expected &&
            InExecutable(im_, expected, p.length) && MatchesAt(im_, p, expected))
        {
            line.found = expected;
            line.matches = 1;
            line.verdict = expected == recorded ? Verdict::AsRecorded : Verdict::Moved;
            return Add(line);
        }
        if (known_.source == Source::Recorded)
        {
            // On the recorded build the offset IS the known address: not there, this rung has failed.
            line.verdict = Verdict::Missing;
            return Add(line);
        }
        if (offsetOnly)
        {
            line.verdict = Verdict::Refused;
            line.note = kOffsetOnly;
            return Add(line);
        }
        // Not at its offset: the function changed around it. One match inside the function is still that anchor.
        line.how = How::FunctionExtent;
        ++report_.scans;
        const Hits hits = CountInExecutable(im_, p, start, end);
        line.matches = hits.count;
        if (hits.count == 1)
        {
            line.found = hits.first;
            line.verdict = hits.first == recorded ? Verdict::AsRecorded : Verdict::Moved;
        }
        else
        {
            line.verdict = hits.count ? Verdict::Ambiguous : Verdict::Missing;
        }
        return Add(line);
    }

    // The start of the function that holds `viaFound`, for a function whose own first bytes do not name it (other
    // functions begin the same way). `pattern` must stand there all the same; `matches` reports how many places in
    // the module begin like it.
    uint32_t FunctionStart(const char* name, uint32_t recordedRva, uint32_t viaFound, const char* pattern)
    {
        Ask(3, name, recordedRva, 0, pattern);
        if (Over())
            return 0;
        const uint32_t recorded = Recorded(recordedRva);
        Line line;
        line.name = name;
        line.recorded = recorded;
        line.how = How::FunctionStart;
        Pattern p;
        if (!Compile(pattern, p))
        {
            line.verdict = Verdict::Refused;
            line.note = "its pattern does not compile";
            return Add(line);
        }
        // How many places begin like it is said only where the module is searched anyway: the address does not
        // come from that count.
        if (known_.source == Source::Search)
        {
            ++report_.scans;
            ++report_.moduleScans;
            line.matches = CountExecutable(im_, p).count;
        }
        else if (const Line* const was = Was())
        {
            line.matches = was->matches;   // what the search that is remembered counted; nothing is counted again
        }
        const uint32_t start = viaFound ? FunctionStartOf(im_, viaFound) : 0;
        if (!start)
        {
            line.verdict = Verdict::NoFunction;
            return Add(line);
        }
        if (!InExecutable(im_, start, p.length) || !MatchesAt(im_, p, start))
        {
            line.verdict = Verdict::Refused;
            line.note = "the function found does not begin with its recorded bytes";
            return Add(line);
        }
        line.found = start;
        line.verdict = start == recorded ? Verdict::AsRecorded : Verdict::Moved;
        return Add(line);
    }

    // An address computed from found ones (`found` 0: what it follows from was not found). `recordedRva` is what
    // the table holds for it, as the cross-check.
    uint32_t Derived(const char* name, uint32_t recordedRva, uint32_t found)
    {
        Ask(4, name, recordedRva, 0, nullptr);
        if (Over())
            return 0;
        const uint32_t recorded = Recorded(recordedRva);
        Line line;
        line.name = name;
        line.recorded = recorded;
        line.found = found;
        line.how = How::Derived;
        line.verdict = !found ? Verdict::Missing : found == recorded ? Verdict::AsRecorded : Verdict::Moved;
        return Add(line);
    }

    // A PROOF ABOUT FOUND ADDRESSES, as a line of its own, so that it counts: a drive is whole only if the proof
    // held, and the report names the one that did not. `value`: what the proof established (a frame's size; 1
    // for a plain yes), 0 when it failed, with `whyNot`. `recordedValue`: what the tables hold for it. Returns
    // `value`.
    uint32_t Proven(const char* name, uint32_t recordedValue, uint32_t value, const char* whyNot)
    {
        Ask(6, name, recordedValue, 0, nullptr);
        if (Over())
            return 0;
        Line line;
        line.name = name;
        line.recorded = Recorded(recordedValue);
        line.found = value;
        line.how = How::Proof;
        if (value)
        {
            line.verdict = value == line.recorded ? Verdict::AsRecorded : Verdict::Moved;
        }
        else
        {
            line.verdict = Verdict::Refused;
            line.note = whyNot ? whyNot : "the proof failed";
        }
        return Add(line);
    }

    // The line added last did not survive a proof: it is no longer found. Always 0, for `x = r.Refuse(...)`.
    uint32_t Refuse(const char* note)
    {
        if (lastStored_)
        {
            Line& line = report_.lines[report_.count - 1];
            line.verdict = Verdict::Refused;
            line.note = note;
            line.found = 0;
        }
        return 0;
    }

    // ---- proofs over found addresses (none adds a line) ----

    // `rva` is where a function starts, by the module's own .pdata.
    bool IsFunctionStart(uint32_t rva) const { return rva && FunctionStartOf(im_, rva) == rva; }

    // `rva` lies in the function that starts at `start`.
    bool SameFunction(uint32_t rva, uint32_t start) const
    {
        return rva && start && FunctionStartOf(im_, rva) == start;
    }

    // Where the `call rel32` (E8) at `callRva` goes, or 0 when there is no such call or it leaves the image.
    uint32_t CallTarget(uint32_t callRva) const
    {
        uint8_t bytes[5];
        if (!callRva || !InExecutable(im_, callRva, 5) || !ReadOriginal(im_, callRva, bytes, 5) || bytes[0] != 0xE8)
            return 0;
        int32_t rel = 0;
        std::memcpy(&rel, bytes + 1, sizeof(rel));
        const int64_t target = static_cast<int64_t>(callRva) + 5 + rel;
        return target > 0 && target < static_cast<int64_t>(im_.size) ? static_cast<uint32_t>(target) : 0;
    }

    // Where a rip-relative operand points: the instruction at `instructionRva`, `length` bytes long, its disp32 at
    // +`displacementOffset`. 0 when it leaves the image.
    uint32_t RipTarget(uint32_t instructionRva, uint32_t displacementOffset, uint32_t length) const
    {
        uint8_t bytes[16];
        if (!instructionRva || length > sizeof(bytes) || displacementOffset + 4 > length ||
            !InExecutable(im_, instructionRva, length) || !ReadOriginal(im_, instructionRva, bytes, length))
            return 0;
        int32_t displacement = 0;
        std::memcpy(&displacement, bytes + displacementOffset, sizeof(displacement));
        const int64_t target = static_cast<int64_t>(instructionRva) + length + displacement;
        return target > 0 && target < static_cast<int64_t>(im_.size) ? static_cast<uint32_t>(target) : 0;
    }

    // THE ENGINE'S TLS INDEX. `operandRva`: the address the drive's own anchored instruction loads the index from
    // (RipTarget of it). It must be the module's own TLS index, as the PE TLS directory names it. The RVA of the
    // index, or 0 with the line refused. `recordedRva`: where the table's build had it.
    uint32_t TlsIndex(const char* name, uint32_t recordedRva, uint32_t operandRva)
    {
        Ask(5, name, recordedRva, 0, nullptr);
        if (Over())
            return 0;
        const uint32_t recorded = Recorded(recordedRva);
        uint32_t directory = im_.tlsIndexRva;
        if (directory && Fault(faults_, kFaultTlsDirectory))
            directory ^= 0x40u;
        Line line;
        line.name = name;
        line.recorded = recorded;
        line.how = How::TlsDirectory;
        if (!directory || !InImage(im_, directory, sizeof(uint32_t)))
        {
            line.verdict = Verdict::Refused;
            line.note = "the module's TLS directory did not give its TLS index";
            return Add(line);
        }
        if (!operandRva)
        {
            line.verdict = Verdict::Refused;
            line.note = "the instruction that loads the TLS index was not found";
            return Add(line);
        }
        if (operandRva != directory)
        {
            line.verdict = Verdict::Refused;
            line.note = "the TLS index the anchored instruction loads is not the one the module's TLS directory names";
            return Add(line);
        }
        line.found = directory;
        line.verdict = directory == recorded ? Verdict::AsRecorded : Verdict::Moved;
        return Add(line);
    }

private:
    // Where the line about to be stored is known to stand: at its recorded RVA, or where an earlier search saved
    // it. (A forced stale save reads every saved address kFaultRecordShift too high.)
    uint32_t KnownAt(uint32_t recorded) const
    {
        if (known_.source == Source::Recorded)
            return recorded;
        if (Fault(faults_, kFaultSavedStale))
            return recorded ? recorded + kFaultRecordShift : 0;
        return report_.count < known_.savedCount ? known_.saved[report_.count] : 0;
    }

    // REMEMBERED: the line an earlier resolution of this very module stored at the place about to be stored.
    const Line* Was() const
    {
        return known_.source == Source::Remembered && known_.remembered && report_.count < known_.remembered->count
                   ? &known_.remembered->lines[report_.count]
                   : nullptr;
    }

    // That line, taken again. One that was found is verified where it was found: its bytes must still stand
    // there, and inside the function it belongs to (`inFunction`: [start, end)). One that was NOT found is not
    // searched for again: it is stored as it was, with the verdict it had. No such line, or bytes that are no
    // longer there: a line not found, which makes this resolution differ from the remembered one.
    Line Again(Line line, const Pattern& p, bool inFunction, uint32_t start, uint32_t end) const
    {
        const Line* const was = Was();
        line.how = How::Known;
        line.verdict = Verdict::Missing;
        if (!was)
            return line;
        if (Found(was->verdict))
        {
            const uint32_t at = was->found;
            const bool there = at && InExecutable(im_, at, p.length) && MatchesAt(im_, p, at) &&
                               (!inFunction || (start && end && at >= start && at < end && p.length <= end - at));
            if (!there)
                return line;
        }
        Line again = *was;
        again.name = line.name;
        return again;
    }

    // How the search placed the saved line about to be verified (None: the entry does not say).
    Placement SavedPlacement() const
    {
        return known_.placed && report_.count < known_.savedCount ? static_cast<Placement>(known_.placed[report_.count])
                                                                  : Placement::None;
    }

    // What the plan asks, into the report's hash: the table's own values, never what was found.
    void Ask(uint8_t kind, const char* name, uint32_t recordedRva, uint32_t functionRva, const char* pattern)
    {
        uint64_t h = report_.plan;
        const auto byte = [&h](uint8_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        const auto word = [&byte](uint32_t v) {
            for (int i = 0; i < 4; ++i)
                byte(static_cast<uint8_t>(v >> (i * 8)));
        };
        const auto text = [&byte](const char* s) {
            for (; s && *s; ++s)
                byte(static_cast<uint8_t>(*s));
            byte(0);
        };
        byte(kind);
        text(name);
        word(recordedRva);
        word(functionRva);
        text(pattern);
        report_.plan = h;
    }

    // A LINE THE REPORT CANNOT HOLD is not looked for and is not found, the same on every rung: a plan must not
    // be given an address that its report does not show. True when the report is full; the line is then counted
    // as dropped, and the caller answers 0.
    bool Over()
    {
        if (report_.count < kMaxLines)
            return false;
        ++report_.dropped;
        lastStored_ = false;
        return true;
    }

    uint32_t Add(const Line& line)
    {
        lastStored_ = report_.count < kMaxLines;
        if (lastStored_)
            report_.lines[report_.count++] = line;
        else
            ++report_.dropped;
        return lastStored_ && Found(line.verdict) ? line.found : 0;
    }

    const Image& im_;
    uint32_t faults_;
    Known known_;
    Report report_{};
    bool lastStored_ = false;
};
} // namespace drive_resolve
