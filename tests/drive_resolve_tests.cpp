// Drive address resolution (src/common/drive_resolve.h) over synthetic images: an anchor found where the table
// says, followed when the code before it grows, refused when its signature is ambiguous or gone, taken at its
// recorded address when that is what tells several matches apart; an anchor that is resolved inside its function;
// function starts and extents from .pdata, chained fragments included; the call, rip-operand and TLS proofs; the
// reads through this mod's own hooks; and every forced fault. Then the rungs (src/common/drive_known.h): the known
// addresses verified with no search, one failed line sending the whole drive to the search, what a search found
// saved and verified the next time, a saved address taken only as the search placed it, what the process already
// resolved in a module verified again with no search (complete, incomplete or refused), and the address cache's
// text with its rules version and checksum.
#define DRIVE_RESOLVE_FAULTS 1
#include "drive_known.h"
#include "drive_resolve.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using namespace drive_resolve;

unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// A synthetic module: `code` bytes of executable section at RVA 0x1000, then a data area (unwind info, a TLS
// index), and a .pdata table held beside it. Filler is 0xCC, which no test pattern contains.
struct Module
{
    static constexpr uint32_t kCode = 0x1000;
    std::vector<uint8_t> bytes;
    std::vector<RuntimeFunction> table;
    uint32_t codeSize = 0;
    uint32_t dataAt = 0;
    Image view{};

    explicit Module(uint32_t code = 0x4000) : codeSize(code)
    {
        bytes.assign(kCode + code + 0x1000, 0xCC);
        dataAt = kCode + code;
        std::memset(bytes.data() + dataAt, 0, 0x1000);
    }

    // Writes "48 8B ..." at `rva` ("??" writes 0xA5) and returns the RVA after it.
    uint32_t Put(uint32_t rva, const char* text)
    {
        Pattern p;
        if (!Compile(text, p))
        {
            Check(false, "a test pattern compiles");
            return rva;
        }
        for (uint32_t i = 0; i < p.length; ++i)
            bytes[rva + i] = p.wild[i] ? 0xA5 : p.bytes[i];
        return rva + p.length;
    }

    void Rel32(uint32_t at, uint32_t nextInstruction, uint32_t target)
    {
        const int32_t rel = static_cast<int32_t>(target) - static_cast<int32_t>(nextInstruction);
        std::memcpy(bytes.data() + at, &rel, sizeof(rel));
    }

    // A .pdata entry with plain unwind info, or one chained to `parent` (an index into `table`).
    uint32_t Function(uint32_t begin, uint32_t end, int parent = -1)
    {
        const uint32_t unwind = dataAt;
        if (parent < 0)
        {
            const uint8_t info[4] = {0x01, 0x04, 0x00, 0x00};   // version 1, no flags, no codes
            std::memcpy(bytes.data() + unwind, info, sizeof(info));
            dataAt += 4;
        }
        else
        {
            const uint8_t info[4] = {0x01 | (4u << 3), 0x00, 0x00, 0x00};   // UNW_FLAG_CHAININFO, no codes
            std::memcpy(bytes.data() + unwind, info, sizeof(info));
            std::memcpy(bytes.data() + unwind + 4, &table[static_cast<size_t>(parent)], sizeof(RuntimeFunction));
            dataAt += 16;
        }
        table.push_back(RuntimeFunction{begin, end, unwind});
        return static_cast<uint32_t>(table.size() - 1);
    }

    const Image& Finish()
    {
        view = Image{};
        view.base = bytes.data();
        view.size = static_cast<uint32_t>(bytes.size());
        view.executable[0] = Range{kCode, codeSize};
        view.executableCount = 1;
        view.functions = table.data();
        view.functionCount = static_cast<uint32_t>(table.size());
        return view;
    }
};

const Line& Last(const Resolver& r)
{
    return r.report().lines[r.report().count - 1];
}

// Distinct byte runs the tests place as "code".
constexpr char kEntry[] = "48 8B C4 55 53 56 57 41 54 48 8D A8 18 FF FF FF 48 81 EC A8 01 00 00";
constexpr char kOther[] = "40 53 48 83 EC 20 8B D9 E8 ?? ?? ?? ?? 48 8B C8";
constexpr char kField[] = "4C 8D 7B 14";                                // short: the instruction that reads a field
constexpr char kStamp[] = "F2 41 0F 10 85 C0 0A 00 00 41 8B 85 C8 0A 00 00";

void TestCompile()
{
    Pattern p;
    Check(Compile("48 8B ?? C0", p) && p.length == 4 && p.bytes[0] == 0x48 && p.wild[2] == 1 && p.bytes[3] == 0xC0,
          "a pattern with a wildcard compiles");
    Check(Compile("48 ? c0", p) && p.length == 3 && p.wild[1] == 1 && p.bytes[2] == 0xC0,
          "a single '?' is a wildcard, and hex is case-blind");
    Check(!Compile("", p) && !Compile(nullptr, p) && !Compile("4G", p) && !Compile("4", p),
          "empty, null and malformed text is refused");
    Check(!Compile("?? ?? ??", p), "a pattern with no byte to match on is refused");
    std::vector<char> big;
    for (uint32_t i = 0; i < kMaxPattern + 1; ++i)
    {
        big.push_back('9');
        big.push_back('0');
        big.push_back(' ');
    }
    big.push_back(0);
    Check(!Compile(big.data(), p), "a pattern longer than the limit is refused");
}

void TestFoundAsRecorded()
{
    Module m;
    m.Put(0x1200, kEntry);
    m.Put(0x1400, kOther);
    Resolver r(m.Finish(), 0);
    Check(r.Unique("entry", 0x1200, kEntry) == 0x1200, "a unique anchor at its recorded RVA is found there");
    Check(Last(r).verdict == Verdict::AsRecorded && Last(r).matches == 1 && Last(r).how == How::Signature,
          "it is reported as recorded, one match, by signature");
    Check(r.report().NotFound() == 0 && r.report().Count(Verdict::AsRecorded) == 1, "the report counts it");
}

void TestMovedAndFollowed()
{
    // The same code after 0x30 bytes were inserted ahead of the function.
    Module m;
    m.Put(0x1230, kEntry);
    Resolver r(m.Finish(), 0);
    Check(r.Unique("entry", 0x1200, kEntry) == 0x1230, "a unique anchor that moved is found where it is now");
    Check(Last(r).verdict == Verdict::Moved && Last(r).recorded == 0x1200 && Last(r).found == 0x1230,
          "it is reported as moved, from the recorded RVA to the found one");
}

void TestAmbiguousRefused()
{
    Module m;
    m.Put(0x1300, kEntry);
    m.Put(0x1900, kEntry);
    Resolver r(m.Finish(), 0);
    Check(r.Unique("entry", 0x1200, kEntry) == 0, "two matches, neither at the recorded RVA: not found");
    Check(Last(r).verdict == Verdict::Ambiguous && Last(r).matches == 2 && Last(r).found == 0,
          "it is reported ambiguous with both matches counted, and the first is not taken");
    Check(r.report().NotFound() == 1, "the report counts the refusal");
}

void TestAmbiguousDisambiguatedByRecord()
{
    Module m;
    m.Put(0x1200, kEntry);
    m.Put(0x1900, kEntry);
    m.Put(0x2100, kEntry);
    Resolver r(m.Finish(), 0);
    Check(r.Unique("entry", 0x1900, kEntry) == 0x1900,
          "several matches, one at the recorded RVA: that one is taken (not the first)");
    Check(Last(r).verdict == Verdict::RecordAmongSeveral && Last(r).matches == 3,
          "it is reported as taken at the recorded address among three");
}

void TestMissingRefused()
{
    Module m;
    m.Put(0x1200, kOther);
    Resolver r(m.Finish(), 0);
    Check(r.Unique("entry", 0x1200, kEntry) == 0 && Last(r).verdict == Verdict::Missing && Last(r).matches == 0,
          "no match: not found, reported missing");
    // A match in data is not code: only the executable sections are searched.
    Module d;
    d.Put(d.dataAt + 0x100, kEntry);
    Resolver rd(d.Finish(), 0);
    Check(rd.Unique("entry", 0x1200, kEntry) == 0, "bytes outside the executable sections never match");
    // A recorded RVA outside the image is only a failed cross-check.
    Module e;
    e.Put(0x1200, kEntry);
    Resolver re(e.Finish(), 0);
    Check(re.Unique("entry", 0xFFFFFF00u, kEntry) == 0x1200 && Last(re).verdict == Verdict::Moved,
          "a recorded RVA outside the image does not stop the search");
    Check(re.Unique("bad", 0x1200, "zz") == 0 && Last(re).verdict == Verdict::Refused,
          "a pattern that does not compile is refused");
}

void TestCountEdges()
{
    Module m(0x100);
    const uint32_t end = Module::kCode + 0x100;
    m.Put(end - 4, kField);            // ends exactly at the end of the executable range
    m.Put(Module::kCode, kField);      // starts exactly at its beginning
    Pattern p;
    Compile(kField, p);
    const Image& im = m.Finish();
    Check(CountExecutable(im, p).count == 2 && CountExecutable(im, p).first == Module::kCode,
          "matches at both edges of a range are counted, the lowest first");
    Check(CountIn(im, p, Module::kCode + 1, end - 1).count == 0, "a match that straddles a range edge is not in it");
    Check(CountIn(im, p, Module::kCode, Module::kCode + 3).count == 0, "a range shorter than the pattern holds no match");
    Check(CountIn(im, p, end, Module::kCode).count == 0 && CountIn(im, p, 0, 0xFFFFFFF0u).count == 0,
          "an empty or out-of-image range holds no match");
    // The count stops at the cap: enough to say "several".
    Module many(0x400);
    for (uint32_t i = 0; i < 40; ++i)
        many.Put(Module::kCode + i * 16, kField);
    Check(CountExecutable(many.Finish(), p).count == kCountCap, "the count is capped");
    // Overlapping matches each count.
    Module over(0x100);
    over.Put(Module::kCode + 8, "AA AA AA");
    Pattern aa;
    Compile("AA AA", aa);
    Check(CountExecutable(over.Finish(), aa).count == 2, "overlapping matches are both counted");
    // A wildcard first byte does not narrow the search.
    Pattern lead;
    Compile("?? 8D 7B 14", lead);
    Check(CountExecutable(im, lead).count == 2, "a leading wildcard matches any byte");
}

// Against a count that compares every offset in full.
void TestCountAgainstBruteForce()
{
    Module m(0x2000);
    uint32_t seed = 0x1234567u;
    for (uint32_t i = 0; i < 0x2000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        m.bytes[Module::kCode + i] = static_cast<uint8_t>((seed >> 24) & 0x3u);   // four byte values: many matches
    }
    const Image& im = m.Finish();
    const char* patterns[] = {"00 01", "03 ?? 03", "?? 02 01 00", "01 01 01 01", "02", "00 ?? ?? 03 03"};
    bool agree = true;
    for (const char* text : patterns)
    {
        Pattern p;
        Compile(text, p);
        uint32_t brute = 0, first = 0;
        for (uint32_t at = Module::kCode; at + p.length <= Module::kCode + 0x2000; ++at)
        {
            bool match = true;
            for (uint32_t i = 0; i < p.length && match; ++i)
                match = p.wild[i] || m.bytes[at + i] == p.bytes[i];
            if (match && !brute++)
                first = at;
        }
        const Hits hits = CountExecutable(im, p);
        agree = agree && hits.count == (brute > kCountCap ? kCountCap : brute) && (!brute || hits.first == first);
    }
    Check(agree, "the anchored search counts what a full compare at every offset counts");
}

void TestFunctionsFromPdata()
{
    Module m;
    m.Function(0x1200, 0x1300);
    const uint32_t head = m.Function(0x1400, 0x1420);
    m.Function(0x1420, 0x1500, static_cast<int>(head));   // a chained fragment of the function at 0x1400
    m.Function(0x1500, 0x1540, static_cast<int>(head));   // and another, contiguous
    m.Function(0x1540, 0x1600);                           // the next function
    m.Function(0x1700, 0x1720, static_cast<int>(head));   // a fragment that is not contiguous with its function
    const Image& im = m.Finish();
    Check(EntryAt(im, 0x1200) && EntryAt(im, 0x12FF) && !EntryAt(im, 0x1300) && !EntryAt(im, 0x11FF),
          "a .pdata entry holds [begin, end)");
    Check(FunctionStartOf(im, 0x1250) == 0x1200 && FunctionEndOf(im, 0x1200) == 0x1300,
          "a plain function starts and ends with its entry");
    Check(FunctionStartOf(im, 0x1450) == 0x1400 && FunctionStartOf(im, 0x1530) == 0x1400,
          "a chained fragment belongs to the function its unwind info names");
    Check(FunctionEndOf(im, 0x1400) == 0x1540, "a function's extent runs over its contiguous chained fragments");
    Check(FunctionStartOf(im, 0x1560) == 0x1540 && FunctionEndOf(im, 0x1540) == 0x1600,
          "and stops at the next function");
    Check(FunctionStartOf(im, 0x1710) == 0x1400, "a detached fragment still names its function");
    Check(FunctionStartOf(im, 0x1650) == 0 && FunctionEndOf(im, 0x1450) == 0,
          "an address in no entry has no function, and a fragment is not a start");
    Resolver r(im, 0);
    Check(r.IsFunctionStart(0x1400) && !r.IsFunctionStart(0x1420) && !r.IsFunctionStart(0x1401) && !r.IsFunctionStart(0),
          "only the first byte of a function is where it starts");
    Check(r.SameFunction(0x1530, 0x1400) && r.SameFunction(0x1710, 0x1400) && !r.SameFunction(0x1560, 0x1400) &&
              !r.SameFunction(0x1650, 0x1400) && !r.SameFunction(0x1450, 0),
          "an address is in a function when .pdata says so");
    // A chain that points outside the image, or loops, resolves to nothing.
    Module bad;
    bad.table.push_back(RuntimeFunction{0x1200, 0x1300, 0xFFFFFF00u});
    Check(FunctionStartOf(bad.Finish(), 0x1250) == 0, "unwind info outside the image is not followed");
    Module loop;
    const uint8_t chain[4] = {0x01 | (4u << 3), 0, 0, 0};
    std::memcpy(loop.bytes.data() + loop.dataAt, chain, 4);
    const RuntimeFunction self{0x1200, 0x1300, loop.dataAt};
    std::memcpy(loop.bytes.data() + loop.dataAt + 4, &self, sizeof(self));
    loop.table.push_back(self);
    Check(FunctionStartOf(loop.Finish(), 0x1250) == 0, "a chain that never ends is given up");
    // An entry that points at another entry instead of unwind info.
    Module indirect;
    const uint32_t real = indirect.Function(0x1200, 0x1300);
    std::memcpy(indirect.bytes.data() + indirect.dataAt, &indirect.table[real], sizeof(RuntimeFunction));
    indirect.table.push_back(RuntimeFunction{0x1300, 0x1340, indirect.dataAt | 1u});
    Check(FunctionStartOf(indirect.Finish(), 0x1320) == 0x1200, "an indirect entry is followed to the one it names");
}

// A function at `start` whose body carries the short field anchor at +0x60 and the stamp anchor at +0x90.
void PutFunction(Module& m, uint32_t start, uint32_t fieldAt = 0x60)
{
    m.Put(start, kEntry);
    m.Put(start + fieldAt, kField);
    m.Put(start + 0x90, kStamp);
    m.Function(start, start + 0x100);
}

void TestInFunction()
{
    // As recorded.
    {
        Module m;
        PutFunction(m, 0x1200);
        m.Put(0x2000, kField);   // the same short bytes in other code: the module-wide count is two
        Resolver r(m.Finish(), 0);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0x1260 && Last(r).verdict == Verdict::AsRecorded &&
                  Last(r).how == How::FunctionOffset,
              "an in-function anchor is found at its recorded offset, whatever the rest of the module holds");
    }
    // The function moved (code inserted ahead of it): the anchor follows it at the same offset.
    {
        Module m;
        PutFunction(m, 0x1750);
        m.Put(0x1260, kField);   // the recorded RVA now holds the same bytes in someone else's code
        Resolver r(m.Finish(), 0);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(entry == 0x1750, "the function is followed");
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0x17B0 && Last(r).verdict == Verdict::Moved &&
                  Last(r).how == How::FunctionOffset,
              "its anchor is found at the same offset from the function's new start, not at the stale RVA");
        Check(r.InFunction("stamp", 0x1290, 0x1200, entry, kStamp) == 0x17E0, "and so is every other anchor in it");
    }
    // The function changed ahead of the anchor: it is no longer at its offset, and is the one match in the function.
    {
        Module m;
        PutFunction(m, 0x1200, 0x68);
        Resolver r(m.Finish(), 0);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0x1268 && Last(r).verdict == Verdict::Moved &&
                  Last(r).how == How::FunctionExtent && Last(r).matches == 1,
              "an anchor that moved inside its function is found as the one match in its extent");
    }
    // Not at its offset and twice in the function: nothing tells them apart.
    {
        Module m;
        PutFunction(m, 0x1200, 0x68);
        m.Put(0x12C0, kField);
        Resolver r(m.Finish(), 0);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0 && Last(r).verdict == Verdict::Ambiguous &&
                  Last(r).matches == 2,
              "two matches inside the function and none at the offset: refused");
    }
    // At its offset AND again later in the function: the offset decides.
    {
        Module m;
        PutFunction(m, 0x1200);
        m.Put(0x12C0, kField);
        Resolver r(m.Finish(), 0);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0x1260 && Last(r).verdict == Verdict::AsRecorded,
              "an anchor at its recorded offset is taken there even where its bytes recur in the function");
    }
    // Gone from the function (the field's offset changed: its displacement bytes are exact).
    {
        Module m;
        m.Put(0x1200, kEntry);
        m.Put(0x1260, "4C 8D 7B 18");
        m.Function(0x1200, 0x1300);
        m.Put(0x2000, kField);   // present elsewhere in the module, which must not count
        Resolver r(m.Finish(), 0);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0 && Last(r).verdict == Verdict::Missing,
              "a field offset that changed is not found: the layout check fails closed");
    }
    // No function: the anchor that locates it was not found, or has no .pdata entry.
    {
        Module m;
        m.Put(0x1200, kEntry);
        m.Put(0x1260, kField);
        Resolver r(m.Finish(), 0);
        Check(r.InFunction("field", 0x1260, 0x1200, 0, kField) == 0 && Last(r).verdict == Verdict::NoFunction,
              "an anchor whose function was not found is not found");
        Check(r.InFunction("field", 0x1260, 0x1200, 0x1200, kField) == 0 && Last(r).verdict == Verdict::NoFunction,
              "nor one whose function has no .pdata entry");
        Check(r.InFunction("field", 0x1100, 0x1200, 0x1200, kField) == 0 && Last(r).verdict == Verdict::NoFunction,
              "nor one recorded ahead of its own function");
    }
    // Located through an anchor in the middle of the function (not its entry), across a chained fragment.
    {
        Module m;
        const uint32_t head = m.Function(0x1800, 0x1820);
        m.Function(0x1820, 0x1900, static_cast<int>(head));
        m.Put(0x1830, kStamp);
        m.Put(0x18A0, kField);
        Resolver r(m.Finish(), 0);
        const uint32_t via = r.Unique("stamp", 0x1230, kStamp);
        Check(via == 0x1830, "the locating anchor is followed");
        Check(r.InFunction("field", 0x12A0, 0x1200, via, kField) == 0x18A0 && Last(r).how == How::FunctionOffset,
              "an anchor in a chained fragment is found at its offset from the function's start");
        Resolver s(m.Finish(), kFaultOffsetMoved);
        const uint32_t via2 = s.Unique("stamp", 0x1230, kStamp);
        Check(s.InFunction("field", 0x12A0, 0x1200, via2, kField) == 0x18A0 && Last(s).how == How::FunctionExtent,
              "and the extent searched for it covers the fragments");
    }
}

void TestFunctionStart()
{
    // Three functions begin with the same bytes; the one wanted is the one that holds the unique stamp.
    Module m;
    for (uint32_t start : {0x1200u, 0x1600u, 0x1A00u})
    {
        m.Put(start, kEntry);
        m.Function(start, start + 0x100);
    }
    m.Put(0x1680, kStamp);
    {
        Resolver r(m.Finish(), 0);
        const uint32_t stamp = r.Unique("stamp", 0x1680, kStamp);
        Check(r.FunctionStart("entry", 0x1600, stamp, kEntry) == 0x1600 && Last(r).verdict == Verdict::AsRecorded &&
                  Last(r).matches == 3 && Last(r).how == How::FunctionStart,
              "a function whose first bytes are shared is found as the start of the function that holds its anchor");
        Check(r.Unique("entry by bytes", 0x1600, kEntry) == 0x1600 && Last(r).verdict == Verdict::RecordAmongSeveral,
              "by its bytes alone it is only told apart by the recorded address");
        Check(r.FunctionStart("entry", 0x1700, stamp, kEntry) == 0x1600 && Last(r).verdict == Verdict::Moved,
              "and with a stale record it is still found, and reported moved");
        Check(r.FunctionStart("entry", 0x1600, stamp, kOther) == 0 && Last(r).verdict == Verdict::Refused,
              "a start that does not begin with the recorded bytes is refused");
        Check(r.FunctionStart("entry", 0x1600, 0, kEntry) == 0 && Last(r).verdict == Verdict::NoFunction,
              "no anchor, no function");
    }
}

void TestProofs()
{
    Module m;
    m.Put(0x1200, kEntry);
    m.Function(0x1200, 0x1300);
    m.bytes[0x1500] = 0xE8;
    m.Rel32(0x1501, 0x1505, 0x1200);
    m.bytes[0x1510] = 0xE9;   // a jump, not a call
    m.Rel32(0x1511, 0x1515, 0x1200);
    m.bytes[0x1520] = 0xE8;
    m.Rel32(0x1521, 0x1525, 0x7FFFFFF0u);   // leaves the image
    // mov ecx, [rip + disp32] -> the "TLS index" in the data area
    const uint32_t index = m.dataAt + 0x800;
    m.Put(0x1600, "8B 0D 00 00 00 00");
    m.Rel32(0x1602, 0x1606, index);
    Image im = m.Finish();
    im.tlsIndexRva = index;
    Resolver r(im, 0);
    Check(r.CallTarget(0x1500) == 0x1200, "a call's target is read from its rel32");
    Check(r.CallTarget(0x1510) == 0 && r.CallTarget(0x1520) == 0 && r.CallTarget(0) == 0 &&
              r.CallTarget(m.dataAt) == 0,
          "what is not a call, leaves the image, or is not code has no target");
    Check(r.RipTarget(0x1600, 2, 6) == index, "a rip-relative operand's target is read from its displacement");
    Check(r.RipTarget(0x1600, 4, 6) == 0 && r.RipTarget(0, 2, 6) == 0 && r.RipTarget(0x1600, 2, 64) == 0,
          "a displacement that does not fit its instruction is refused");
    Check(r.Derived("call", 0x1500, 0x1500) == 0x1500 && Last(r).verdict == Verdict::AsRecorded &&
              Last(r).how == How::Derived,
          "a derived address that agrees with the table is reported as recorded");
    Check(r.Derived("call", 0x1400, 0x1500) == 0x1500 && Last(r).verdict == Verdict::Moved,
          "one that disagrees is reported moved");
    Check(r.Derived("call", 0x1500, 0) == 0 && Last(r).verdict == Verdict::Missing, "one that was not derived is missing");
    Check(r.Refuse("its target is not the entry") == 0 && Last(r).verdict == Verdict::Refused && Last(r).found == 0 &&
              std::strcmp(Last(r).note, "its target is not the entry") == 0,
          "a line that fails a proof is refused, with the proof named");

    // The engine TLS index: the PE TLS directory and the anchored instruction must name one address.
    Check(r.TlsIndex("TLS index", index, r.RipTarget(0x1600, 2, 6)) == index && Last(r).verdict == Verdict::AsRecorded &&
              Last(r).how == How::TlsDirectory,
          "the TLS index is the directory's when the instruction loads it from there");
    Check(r.TlsIndex("TLS index", index + 0x40, index) == index && Last(r).verdict == Verdict::Moved,
          "a stale record of it is reported, and the directory's address is used");
    Check(r.TlsIndex("TLS index", index, index + 8) == 0 && Last(r).verdict == Verdict::Refused,
          "an instruction that loads another address is refused");
    Check(r.TlsIndex("TLS index", index, 0) == 0 && Last(r).verdict == Verdict::Refused,
          "an instruction that was not found is refused");
    Image none = m.Finish();
    Resolver rn(none, 0);
    Check(rn.TlsIndex("TLS index", index, index) == 0 && Last(rn).verdict == Verdict::Refused,
          "a module whose TLS directory did not read has no TLS index");
    Image outside = m.Finish();
    outside.tlsIndexRva = outside.size - 2;
    Resolver ro(outside, 0);
    Check(ro.TlsIndex("TLS index", index, outside.size - 2) == 0, "a TLS index that does not fit the image is refused");
}

void TestFaults()
{
    Module m;
    PutFunction(m, 0x1200);
    const uint32_t index = m.dataAt + 0x800;
    Image im = m.Finish();
    im.tlsIndexRva = index;
    {
        Resolver r(im, kFaultSignatureMissing);
        Check(r.Unique("entry", 0x1200, kEntry) == 0 && Last(r).verdict == Verdict::Missing,
              "fault: a signature that finds nothing leaves the anchor not found");
    }
    {
        Resolver r(im, kFaultSignatureAmbiguous);
        Check(r.Unique("entry", 0x1200, kEntry) == 0x1200 && Last(r).verdict == Verdict::RecordAmongSeveral &&
                  Last(r).matches == 2,
              "fault: an ambiguous signature is settled by the recorded address");
    }
    {
        Resolver r(im, kFaultRecordWrong);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(entry == 0x1200 && Last(r).verdict == Verdict::Moved && Last(r).recorded == 0x1200 + kFaultRecordShift,
              "fault: with a wrong record the search wins and the anchor is reported moved");
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0x1260 && Last(r).verdict == Verdict::Moved &&
                  Last(r).how == How::FunctionOffset,
              "fault: an in-function anchor follows its function all the same");
        Check(r.Recorded(0x1200) == 0x1200 + kFaultRecordShift && r.Recorded(0) == 0, "fault: every record reads shifted");
    }
    {
        Resolver r(im, kFaultSignatureAmbiguous | kFaultRecordWrong);
        Check(r.Unique("entry", 0x1200, kEntry) == 0 && Last(r).verdict == Verdict::Ambiguous,
              "fault: ambiguous with a wrong record is refused");
    }
    {
        Resolver r(im, kFaultOffsetMoved);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0x1260 && Last(r).how == How::FunctionExtent,
              "fault: an anchor denied its offset is found as the one match in its function");
    }
    {
        Resolver r(im, kFaultTlsDirectory);
        Check(r.TlsIndex("TLS index", index, index) == 0 && Last(r).verdict == Verdict::Refused,
              "fault: a TLS directory that disagrees is refused");
    }
    {
        Resolver r(im, kFaultHookSilent);
        Check(r.Unique("entry", 0x1200, kEntry) == 0x1200 && Last(r).verdict == Verdict::AsRecorded,
              "the watchdog's fault bit changes nothing about where things are");
    }
    Check(Fault(kFaultSignatureMissing, kFaultSignatureMissing) && !Fault(0, kFaultSignatureMissing),
          "a fault is forced only by its own bit");
}

// THIS MOD'S OWN HOOKS. A hooked function's first bytes are a jump; proofs read what stood there before.
uint32_t g_patchAt = 0;
uint8_t g_patchOriginal[5]{};
const uint8_t* g_patchBase = nullptr;

uint32_t FakeOriginal(uintptr_t address, void* out, size_t size)
{
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    const uintptr_t patch = reinterpret_cast<uintptr_t>(g_patchBase) + g_patchAt;
    uint32_t undone = 0;
    for (size_t i = 0; i < 5; ++i)
        if (patch + i >= address && patch + i < address + size)
        {
            static_cast<uint8_t*>(out)[patch + i - address] = g_patchOriginal[i];
            undone = 1;
        }
    return undone;
}

void FakeCorrect(uintptr_t begin, size_t size, const uint8_t* bytes, const uint8_t* wild, size_t n, uintptr_t& first,
                 uint32_t& count)
{
    // The count over the range with the patch undone, by a full compare at every offset.
    std::vector<uint8_t> copy(size);
    (void)FakeOriginal(begin, copy.data(), size);
    first = 0;
    count = 0;
    for (size_t at = 0; at + n <= size; ++at)
    {
        bool match = true;
        for (size_t i = 0; i < n && match; ++i)
            match = wild[i] || copy[at + i] == bytes[i];
        if (match && !count++)
            first = begin + at;
    }
}

void TestThroughOwnHooks()
{
    Module m;
    PutFunction(m, 0x1200);
    m.bytes[0x1500] = 0xE8;
    m.Rel32(0x1501, 0x1505, 0x1200);
    // Hook the function: E9 rel32 over its first five bytes.
    g_patchAt = 0x1200;
    std::memcpy(g_patchOriginal, m.bytes.data() + 0x1200, 5);
    m.bytes[0x1200] = 0xE9;
    m.Rel32(0x1201, 0x1205, 0x3000);
    Image im = m.Finish();
    g_patchBase = im.base;
    {
        Resolver r(im, 0);
        Check(r.Unique("entry", 0x1200, kEntry) == 0 && Last(r).verdict == Verdict::Missing,
              "control: read as it stands, a hooked function's signature is gone");
    }
    im.original = &FakeOriginal;
    im.correct = &FakeCorrect;
    {
        Resolver r(im, 0);
        const uint32_t entry = r.Unique("entry", 0x1200, kEntry);
        Check(entry == 0x1200 && Last(r).verdict == Verdict::AsRecorded && Last(r).matches == 1,
              "read through the hook, the function is found by the game's own bytes");
        Check(r.InFunction("field", 0x1260, 0x1200, entry, kField) == 0x1260, "and its anchors with it");
        Pattern p;
        Compile(kEntry, p);
        Check(MatchesAt(im, p, 0x1200), "a compare at a fixed place reads the game's bytes too");
    }
    // A hook on the call site itself: the call proof reads the original instruction.
    g_patchAt = 0x1500;
    std::memcpy(g_patchOriginal, m.bytes.data() + 0x1500, 5);
    m.bytes[0x1500] = 0xE9;
    {
        Resolver r(im, 0);
        Check(r.CallTarget(0x1500) == 0x1200, "a hooked call site still proves its original target");
    }
    g_patchBase = nullptr;
}

// A minimal PE32+ header over a buffer, to read the executable sections and .pdata the way the DLL does.
void TestReadImage()
{
    std::vector<uint8_t> pe(0x6000, 0);
    const auto put16 = [&pe](size_t at, uint16_t v) { std::memcpy(pe.data() + at, &v, sizeof(v)); };
    const auto put32 = [&pe](size_t at, uint32_t v) { std::memcpy(pe.data() + at, &v, sizeof(v)); };
    const size_t nt = 0x80, optional = nt + 24, table = optional + 240;
    put16(0, 0x5A4D);
    put32(0x3C, static_cast<uint32_t>(nt));
    put32(nt, 0x00004550u);
    put16(nt + 4, 0x8664);
    put16(nt + 6, 3);        // sections
    put16(nt + 20, 240);     // optional header size
    put16(optional, 0x20B);
    put32(optional + 108, 16);
    // .text (executable), .rdata, .pdata
    put32(table + 8, 0x2000);
    put32(table + 12, 0x1000);
    put32(table + 36, 0x60000020u);
    put32(table + 40 + 8, 0x1000);
    put32(table + 40 + 12, 0x3000);
    put32(table + 40 + 36, 0x40000040u);
    put32(table + 80 + 8, 0x1000);
    put32(table + 80 + 12, 0x4000);
    put32(table + 80 + 36, 0x40000040u);
    // The exception directory: two entries and a zeroed third.
    put32(optional + 112 + 3 * 8, 0x4000);
    put32(optional + 112 + 3 * 8 + 4, 36);
    const RuntimeFunction entries[2] = {{0x1100, 0x1200, 0x3000}, {0x1200, 0x1280, 0x3004}};
    std::memcpy(pe.data() + 0x4000, entries, sizeof(entries));
    pe[0x3000] = 0x01;
    pe[0x3004] = 0x01;

    Image im;
    const char* why = nullptr;
    Check(ReadImage(pe.data(), pe.size(), im, why) && why == nullptr, "a PE32+ image's headers are read");
    Check(im.executableCount == 1 && im.executable[0].rva == 0x1000 && im.executable[0].size == 0x2000,
          "its executable sections are the ones marked executable");
    Check(im.functionCount == 2 && FunctionStartOf(im, 0x1250) == 0x1200, "its .pdata is read, without the zeroed tail");
    Check(im.tlsIndexRva == 0, "the TLS index is left to the image's owner");

    // A function table that cannot be read is no table: the owner is asked before any of it is read (its zeroed
    // tail is the first thing a reader would look at), and the rest of the image is read all the same.
    static uintptr_t unreadableFrom = 0, unreadableTo = 0;
    static unsigned asked = 0;
    const auto readable = [](uintptr_t address, size_t size) {
        ++asked;
        return address + size <= unreadableFrom || address >= unreadableTo;
    };
    unreadableFrom = reinterpret_cast<uintptr_t>(pe.data()) + 0x4000;
    unreadableTo = unreadableFrom + 0x1000;
    Check(ReadImage(pe.data(), pe.size(), im, why, readable) && !why && asked == 1 && im.functions == nullptr &&
              im.functionCount == 0 && im.executableCount == 1 && im.readable != nullptr &&
              FunctionStartOf(im, 0x1250) == 0,
          "a function table that cannot be read is no table: the image is read, and no function is known in it");
    unreadableFrom = unreadableTo = 0;
    asked = 0;
    Check(ReadImage(pe.data(), pe.size(), im, why, readable) && asked == 1 && im.functionCount == 2 &&
              im.readable != nullptr && FunctionStartOf(im, 0x1250) == 0x1200,
          "one that can be read is read, and the image keeps the owner's word for every later read");

    std::vector<uint8_t> bad = pe;
    bad[0] = 0;
    Check(!ReadImage(bad.data(), bad.size(), im, why) && why, "no DOS header is refused, with the reason");
    bad = pe;
    put32(nt, 0);
    std::memcpy(bad.data(), pe.data(), pe.size());
    Check(!ReadImage(bad.data(), bad.size(), im, why), "no PE header is refused");
    put32(nt, 0x00004550u);
    bad = pe;
    bad[optional] = 0x0B;
    bad[optional + 1] = 0x01;   // PE32, not PE32+
    Check(!ReadImage(bad.data(), bad.size(), im, why), "a 32-bit image is refused");
    bad = pe;
    std::memcpy(bad.data() + table + 12, "\x00\x00\x10\x00", 4);   // .text at an RVA outside the image
    Check(!ReadImage(bad.data(), bad.size(), im, why), "a section outside the image is refused");
    bad = pe;
    std::memcpy(bad.data() + optional + 112 + 3 * 8, "\x00\x00\x20\x00", 4);
    Check(!ReadImage(bad.data(), bad.size(), im, why), "an exception directory outside the image is refused");
    bad = pe;
    std::memcpy(bad.data() + table + 36, "\x40\x00\x00\x40", 4);   // no executable section left
    Check(!ReadImage(bad.data(), bad.size(), im, why), "an image with no executable section is refused");
    Check(!ReadImage(nullptr, 0x6000, im, why) && !ReadImage(pe.data(), 0x100, im, why),
          "a null or tiny image is refused");
}

void TestReport()
{
    Module m;
    PutFunction(m, 0x1200);
    Resolver a(m.Finish(), 0), b(m.Finish(), 0), c(m.Finish(), kFaultRecordWrong);
    a.Unique("entry", 0x1200, kEntry);
    b.Unique("entry", 0x1200, kEntry);
    c.Unique("entry", 0x1200, kEntry);
    Check(a.report().Fingerprint() == b.report().Fingerprint(), "the same resolution has the same fingerprint");
    Check(a.report().Fingerprint() != c.report().Fingerprint(),
          "the same address found another way has another fingerprint");
    Check(std::strcmp(VerdictText(Verdict::Moved), "MOVED") == 0 && std::strcmp(HowText(How::Signature), "signature") == 0 &&
              Found(Verdict::RecordAmongSeveral) && !Found(Verdict::Ambiguous) && !Found(Verdict::Refused),
          "verdicts read as text, and only three of them are a find");
    // More lines than the report holds: a line it cannot hold is counted, is not looked for, and is NOT found.
    Resolver many(m.Finish(), 0);
    uint32_t found = 0;
    for (uint32_t i = 0; i < kMaxLines + 5; ++i)
        found += many.Unique("entry", 0x1200, kEntry) == 0x1200 ? 1u : 0u;
    Check(found == kMaxLines && many.report().count == kMaxLines && many.report().dropped == 5 &&
              many.report().scans == kMaxLines && !many.report().Complete(),
          "a line beyond the report's size is counted, is not searched for, and is not found: a plan is given no "
          "address that its report does not show");
    Check(many.Refuse("x") == 0 && many.report().lines[kMaxLines - 1].verdict == Verdict::AsRecorded,
          "refusing a line that was not stored touches no other line");
    Check(many.InFunction("field", 0x1260, 0x1200, 0x1200, kField) == 0 &&
              many.FunctionStart("start", 0x1200, 0x1200, kEntry) == 0 &&
              many.Derived("derived", 0x1200, 0x1200) == 0 && many.Proven("proof", 1, 1, nullptr) == 0 &&
              many.TlsIndex("tls", 0x10, 0x10) == 0 &&
              many.report().dropped == 10 && many.report().scans == kMaxLines,
          "whatever kind of line it is");
}
// ------------------------------------------------------------------------------------------------ the rungs
// A small drive written as drive_sites.h writes them: a hooked function found by signature that must be where a
// function starts, a field anchor inside it, a second signature, and a call derived from that one which must land
// on the entry.
struct Mini
{
    uint32_t entry = 0, field = 0, call = 0;
    bool ok = false;
};
constexpr uint32_t kMiniEntry = 0x1200, kMiniField = 0x1260, kMiniCallSite = 0x2000;

Mini MiniPlan(Resolver& r)
{
    Mini f;
    f.entry = r.Unique("entry", kMiniEntry, kEntry);
    if (f.entry && !r.IsFunctionStart(f.entry))
        f.entry = r.Refuse("it is not where a function starts");
    f.field = r.InFunction("field", kMiniField, kMiniEntry, f.entry, kField);
    const uint32_t site = r.Unique("call site", kMiniCallSite, kOther);   // its E8 is the ninth byte
    f.call = r.Derived("call", kMiniCallSite + 8, site ? site + 8 : 0);
    if (f.call && !(f.entry && r.CallTarget(f.call) == f.entry))
        f.call = r.Refuse("it does not call the entry");
    f.ok = f.entry && f.field && f.call;
    return f;
}

// The module that plan describes, with everything `shift` bytes later than the plan records it.
void BuildMini(Module& m, uint32_t shift, uint32_t fieldAt = 0x60)
{
    PutFunction(m, kMiniEntry + shift, fieldAt);
    m.Put(kMiniCallSite + shift, kOther);
    m.Rel32(kMiniCallSite + shift + 9, kMiniCallSite + shift + 13, kMiniEntry + shift);
}

drive_known::Outcome RunMini(const Image& image, uint32_t faults, bool recordedBuild, const drive_known::Entry* saved,
                             Mini& found, Report& report)
{
    const auto plan = [](Resolver& r) { return MiniPlan(r); };
    return drive_known::RunTyped(image, faults, recordedBuild, saved, plan, found, report);
}

bool AllHow(const Report& report, How signatures)
{
    // The two signature lines (entry, call site) by `signatures`; the field by its offset or as known; the call derived.
    return report.count == 4 && report.lines[0].how == signatures && report.lines[2].how == signatures &&
           report.lines[3].how == How::Derived;
}

void TestRungRecorded()
{
    Module m;
    BuildMini(m, 0);
    m.Put(0x3000, kEntry);   // a second place with the entry's bytes: a search would have to tell them apart
    const Image& image = m.Finish();
    Mini found;
    Report report;
    drive_known::Outcome out = RunMini(image, 0, true, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Recorded && out.scans == 0 && report.scans == 0 && out.moduleScans == 0,
          "on the recorded build the recorded addresses are verified with no search of the module at all");
    Check(found.ok && found.entry == kMiniEntry && found.field == kMiniField && found.call == kMiniCallSite + 8 &&
              report.Complete() && report.Count(Verdict::AsRecorded) == 4 && AllHow(report, How::Known),
          "every line verified where it is recorded, and every derived proof made on those addresses");
    // The search on the same module chooses the same addresses (the entry among two, by its record).
    Resolver search(image, 0);
    const Mini searched = MiniPlan(search);
    Check(searched.entry == found.entry && searched.field == found.field && searched.call == found.call &&
              search.report().lines[0].verdict == Verdict::RecordAmongSeveral && search.report().scans == 2,
          "the search of that module chooses the same addresses, and needs two scans for them");
    Check(search.report().plan == report.plan && report.plan != Report{}.plan,
          "what was asked hashes the same whether it was verified or searched for");

    // The same module when it is NOT known to be the recorded build: nothing is known, the search runs, and what
    // it finds is worth saving even though nothing moved.
    out = RunMini(image, 0, false, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Search && !out.recordedTried && !out.savedTried && out.scans == 2 && out.learn &&
              found.ok && AllHow(report, How::Signature),
          "a build that is not the recorded one is searched, and its addresses are worth saving");
}

void TestRungOneFailsWhole()
{
    // The recorded build by its stamp, but the field no longer stands at its recorded offset in the function.
    Module m;
    BuildMini(m, 0, 0x70);
    const Image& image = m.Finish();
    Mini found;
    Report report;
    const drive_known::Outcome out = RunMini(image, 0, true, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Search && out.recordedTried && found.ok && found.field == kMiniEntry + 0x70,
          "one line that does not verify sends the whole drive to the search, which finds it");
    Check(AllHow(report, How::Signature) && report.lines[1].how == How::FunctionExtent &&
              report.lines[1].verdict == Verdict::Moved && out.scans == 3,
          "the result in use is the search's alone: no line of it was taken from the failed rung");
    Check(out.moduleScans == 2 && report.moduleScans == 2,
          "of its three searches two were of the whole module (the signatures) and one of a single function");
    Check(out.learn, "a site that moved on the recorded build is worth saving too");

    // A function start that is not one fails rung 1 the same way a search would refuse it.
    Module n;
    n.Put(kMiniEntry, kEntry);
    n.Put(kMiniEntry + 0x60, kField);
    n.Function(kMiniEntry - 0x20, kMiniEntry + 0x100);   // the entry's bytes sit inside another function
    n.Put(kMiniCallSite, kOther);
    n.Rel32(kMiniCallSite + 9, kMiniCallSite + 13, kMiniEntry);
    const drive_known::Outcome refused = RunMini(n.Finish(), 0, true, nullptr, found, report);
    Check(refused.rung == drive_known::Rung::Search && refused.recordedTried && !found.ok && !found.entry &&
              !report.Complete() && !refused.learn,
          "bytes at the known address are not enough: the proofs are made on it, and a failed one is not learned");
}

void TestRungSaved()
{
    constexpr uint32_t kShift = 0x340;
    Module m;
    BuildMini(m, kShift);
    m.Finish();
    m.view.timeDateStamp = 0x68A0EDFC;
    m.view.sizeOfImage = 0x04768000;
    const Image& image = m.view;
    Check(drive_known::Stamp(image) == 0x68A0EDFC04768000ull, "the stamp is the PE timestamp and the image size");
    Mini found;
    Report report;
    // First launch on this build: nothing known, searched, worth saving.
    drive_known::Outcome out = RunMini(image, 0, false, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Search && out.learn && out.scans == 2 && found.ok &&
              found.entry == kMiniEntry + kShift && report.Count(Verdict::Moved) == 4,
          "a moved build: found by search, every line reported moved");
    drive_known::Entry entry;
    Check(drive_known::MakeEntry(entry, "halo3.dll", "control-update", drive_known::Stamp(image), report) &&
              entry.count == 4 && entry.plan == report.plan && entry.rvas[0] == kMiniEntry + kShift &&
              entry.rvas[3] == kMiniCallSite + kShift + 8,
          "what the search found becomes an entry: every line's address, the stamp, the plan");
    Check(entry.rules == kRulesVersion && entry.placed[0] == 'U' && entry.placed[1] == 'O' && entry.placed[2] == 'U' &&
              entry.placed[3] == 'D',
          "and, with each address, how the search placed it, under the rules it searched by");
    // Second launch: verified at the saved addresses, no search.
    Mini again;
    Report second;
    out = RunMini(image, 0, false, &entry, again, second);
    Check(out.rung == drive_known::Rung::Saved && out.scans == 0 && out.savedTried && !out.savedRefused && !out.learn,
          "the next launch verifies the saved addresses with no search of the module");
    Check(again.ok && again.entry == found.entry && again.field == found.field && again.call == found.call &&
              second.Complete() && second.Count(Verdict::Moved) == 4 && AllHow(second, How::Known) &&
              second.lines[1].how == How::Known && drive_known::SameAddresses(second, report),
          "and has the same addresses, each still reported as not where the tables record it");
    drive_known::Entry notASearch;
    Check(!drive_known::MakeEntry(notASearch, "halo3.dll", "control-update", drive_known::Stamp(image), second),
          "a result that was only verified does not become an entry: only a search says how each line was placed");

    // The same stamp, but the code is elsewhere now: the saved addresses do not verify, and the search runs.
    Module other;
    BuildMini(other, 0x400);
    out = RunMini(other.Finish(), 0, false, &entry, again, second);
    Check(out.rung == drive_known::Rung::Search && out.savedTried && out.savedRefused == drive_known::kSavedStale &&
              again.ok && again.entry == kMiniEntry + 0x400 && out.learn && AllHow(second, How::Signature),
          "stale saved addresses fall through to the search, whose result alone is used");
    // Saved for another version of the drive's tables.
    drive_known::Entry otherPlan = entry;
    otherPlan.plan ^= 1;
    out = RunMini(image, 0, false, &otherPlan, again, second);
    Check(out.rung == drive_known::Rung::Search && out.savedRefused == drive_known::kSavedOtherPlan && again.ok,
          "addresses saved for another version of the tables are not used, even where they verify");
    drive_known::Entry shorter = entry;
    --shorter.count;
    out = RunMini(image, 0, false, &shorter, again, second);
    Check(out.rung == drive_known::Rung::Search && out.savedRefused != nullptr && again.ok,
          "nor an entry with another number of lines");
    drive_known::Entry shortest = entry;
    shortest.count = 2;   // no address for the third line, a signature
    out = RunMini(image, 0, false, &shortest, again, second);
    Check(out.rung == drive_known::Rung::Search && out.savedTried && out.savedRefused == drive_known::kSavedOtherPlan &&
              again.ok,
          "an entry that holds no address for a line that is asked for is of another version of the tables, and the "
          "reason given is that, not that the game DLL changed");
    // Found under other rules of the search.
    drive_known::Entry otherRules = entry;
    ++otherRules.rules;
    out = RunMini(image, 0, false, &otherRules, again, second);
    Check(out.rung == drive_known::Rung::Search && !out.savedTried && out.savedRefused == drive_known::kOtherRules &&
              again.ok,
          "addresses found under other rules of the search are not tried at all, though each of them would verify");
    // A derived address that no longer follows from the verified ones.
    drive_known::Entry bent = entry;
    ++bent.rvas[3];
    out = RunMini(image, 0, false, &bent, again, second);
    Check(out.rung == drive_known::Rung::Search && out.savedRefused == drive_known::kSavedStale && again.ok,
          "nor one whose derived addresses are not what follows from the verified ones");
    // A saved field address outside its function.
    drive_known::Entry outside = entry;
    outside.rvas[1] = kMiniCallSite + kShift;
    out = RunMini(image, 0, false, &outside, again, second);
    Check(out.rung == drive_known::Rung::Search && out.savedRefused == drive_known::kSavedStale && again.ok,
          "nor one whose field anchor is not inside its function");
    // On the recorded build the recorded addresses come first, and an entry is not needed.
    Module recorded;
    BuildMini(recorded, 0);
    out = RunMini(recorded.Finish(), 0, true, &entry, again, second);
    Check(out.rung == drive_known::Rung::Recorded && !out.savedTried && out.scans == 0,
          "on the recorded build the recorded addresses are tried first and suffice");
}

// A SAVED ADDRESS IS TAKEN ONLY AS THE SEARCH THAT SAVED IT WOULD TAKE IT AGAIN. Each entry here is one a search
// made, or that entry with one address or one placement changed; an entry has no checksum until it is written to
// a file, so what is refused here is refused by the verification alone.
void TestSavedPlacement()
{
    using drive_known::Entry;
    using drive_known::kSavedStale;
    using drive_known::MakeEntry;
    using drive_known::Rung;
    constexpr uint32_t kShift = 0x340, kEntryAt = kMiniEntry + kShift, kSecondSite = 0x3000;
    Mini found, again;
    Report report, second;
    drive_known::Outcome out;

    // AN IN-FUNCTION ROW SAVED "AT ITS RECORDED OFFSET" IS TAKEN ONLY THERE. The field's bytes stand at the offset
    // and once more, later in the same function.
    {
        Module m;
        BuildMini(m, kShift);
        m.Put(kEntryAt + 0xC0, kField);
        const Image& image = m.Finish();
        out = RunMini(image, 0, false, nullptr, found, report);
        Entry entry;
        Check(out.rung == Rung::Search && found.ok && found.field == kMiniField + kShift &&
                  MakeEntry(entry, "halo3.dll", "control-update", 1, report) && entry.placed[1] == 'O',
              "a field found at its recorded offset is saved as placed there");
        out = RunMini(image, 0, false, &entry, again, second);
        Check(out.rung == Rung::Saved && out.scans == 0 && again.field == found.field,
              "and is verified there with no search");
        Entry elsewhere = entry;
        elsewhere.rvas[1] = kEntryAt + 0xC0;   // the same bytes, inside the same function
        out = RunMini(image, 0, false, &elsewhere, again, second);
        Check(out.rung == Rung::Search && out.savedRefused == kSavedStale && again.field == found.field,
              "a row saved at its recorded offset is accepted only there: the same bytes elsewhere in the function are not it");
        elsewhere.placed[1] = 'X';   // ... nor by saying it was the function's single match
        out = RunMini(image, 0, false, &elsewhere, again, second);
        Check(out.rung == Rung::Search && out.savedRefused == kSavedStale && again.field == found.field,
              "nor as the function's single match while the bytes stand at the offset, where the search looks first");
    }

    // A ROW SAVED AS "THE SINGLE MATCH IN ITS FUNCTION" HAS THAT FUNCTION SEARCHED AGAIN.
    {
        Module m;
        BuildMini(m, kShift, 0x68);   // the field is not at its recorded offset (0x60)
        const Image& image = m.Finish();
        out = RunMini(image, 0, false, nullptr, found, report);
        Entry entry;
        Check(out.rung == Rung::Search && found.ok && found.field == kEntryAt + 0x68 &&
                  report.lines[1].how == How::FunctionExtent &&
                  MakeEntry(entry, "halo3.dll", "control-update", 1, report) && entry.placed[1] == 'X',
              "a field found as the single match in its function is saved as placed so");
        out = RunMini(image, 0, false, &entry, again, second);
        Check(out.rung == Rung::Saved && out.scans == 1 && second.scans == 1 && second.lines[1].matches == 1 &&
                  again.field == found.field,
              "it is verified with its function searched again, and nothing else searched");
        Entry asOffset = entry;
        asOffset.placed[1] = 'O';
        out = RunMini(image, 0, false, &asOffset, again, second);
        Check(out.rung == Rung::Search && out.savedRefused == kSavedStale && again.ok,
              "the same address saved as \"at its offset\" is not accepted: it is not at its offset");
        // The same code with those bytes twice in the function: the search refuses, and so does the saved address.
        Module twice;
        BuildMini(twice, kShift, 0x68);
        twice.Put(kEntryAt + 0xC0, kField);
        Resolver search(twice.Finish(), 0);
        const Mini searched = MiniPlan(search);
        Check(!searched.field && search.report().lines[1].verdict == Verdict::Ambiguous,
              "two matches in the function and none at the offset: the search refuses the row");
        out = RunMini(twice.Finish(), 0, false, &entry, again, second);
        Check(out.rung == Rung::Search && out.savedRefused == kSavedStale && !again.field && !again.ok,
              "and a saved address that is one of the two is not accepted where the search refuses");
        Entry later = entry;
        later.rvas[1] = kEntryAt + 0xC0;
        out = RunMini(twice.Finish(), 0, false, &later, again, second);
        Check(out.rung == Rung::Search && out.savedRefused == kSavedStale && !again.field,
              "nor the other of the two");
    }

    // A SIGNATURE WITH TWO MATCHES IN THE MODULE. The call site's bytes stand twice, and both calls land on the
    // entry, so every proof of the plan holds at either.
    {
        // One of the two stands at the recorded address: the search takes that one, and saves it as placed so.
        Module m;
        BuildMini(m, 0);
        m.Put(kSecondSite, kOther);
        m.Rel32(kSecondSite + 9, kSecondSite + 13, kMiniEntry);
        const Image& image = m.Finish();
        out = RunMini(image, 0, false, nullptr, found, report);
        Entry entry;
        Check(out.rung == Rung::Search && found.ok && found.call == kMiniCallSite + 8 &&
                  report.lines[2].verdict == Verdict::RecordAmongSeveral &&
                  MakeEntry(entry, "halo3.dll", "control-update", 1, report) && entry.placed[2] == 'R',
              "of two matches the search takes the one at the recorded address, and saves it as placed so");
        out = RunMini(image, 0, false, &entry, again, second);
        Check(out.rung == Rung::Saved && out.scans == 0 && again.call == found.call, "which is verified there");
        Entry other = entry;
        other.rvas[2] = kSecondSite;
        other.rvas[3] = kSecondSite + 8;
        out = RunMini(image, 0, false, &other, again, second);
        Check(out.rung == Rung::Search && out.savedRefused == kSavedStale && again.call == found.call,
              "A SAVED ADDRESS THAT IS THE SECOND MATCH is not accepted for a row taken at its recorded address");
        other.placed[2] = 'U';
        out = RunMini(image, 0, false, &other, again, second);
        Check(out.rung == Rung::Search && out.savedRefused == kSavedStale && again.call == found.call,
              "nor as \"the single match\": the bytes stand at the recorded address, where the search takes them");
    }
    {
        // Neither stands at the recorded address: the search refuses the row, and there is nothing to save.
        Module m;
        BuildMini(m, kShift);
        m.Put(kSecondSite, kOther);
        m.Rel32(kSecondSite + 9, kSecondSite + 13, kEntryAt);
        const Image& image = m.Finish();
        out = RunMini(image, 0, false, nullptr, found, report);
        Entry none;
        Check(out.rung == Rung::Search && !found.ok && report.lines[2].verdict == Verdict::Ambiguous && !out.learn &&
                  !MakeEntry(none, "halo3.dll", "control-update", 1, report),
              "two matches and none at the recorded address: the search refuses, and nothing is saved");
        // What a search found in the same code while the bytes stood there once, with its third line moved to the
        // second match.
        Module once;
        BuildMini(once, kShift);
        Report single;
        (void)RunMini(once.Finish(), 0, false, nullptr, found, single);
        Entry entry;
        Check(MakeEntry(entry, "halo3.dll", "control-update", 1, single) && entry.placed[2] == 'U',
              "the same code with one match: found, and saved as the single match");
        Entry other = entry;
        other.rvas[2] = kSecondSite;
        other.rvas[3] = kSecondSite + 8;
        other.placed[2] = 'R';
        out = RunMini(image, 0, false, &other, again, second);
        Check(out.rung == Rung::Search && out.savedRefused == kSavedStale && !again.ok,
              "the second match saved as \"taken at the recorded address\" is not accepted: it is not at that address");
        // THE LIMIT, STATED. An entry that says "the single match in the module" for one of two matches, neither
        // of them at the recorded address, is taken at its word: the bytes stand at the saved address, they do
        // not stand at the recorded one, and only counting the whole module's matches -- the search itself --
        // could tell. No search writes such an entry (it refuses the row, above); a file holds one only if it was
        // edited and its checksum computed again, or if the DLL's bytes changed under the same PE timestamp and
        // image size. Everything the plan proves about the address is still proved on it.
        other.placed[2] = 'U';
        out = RunMini(image, 0, false, &other, again, second);
        Check(out.rung == Rung::Saved && out.scans == 0 && again.call == kSecondSite + 8,
              "the limit: a second match saved as \"the single match\", with none at the recorded address, is taken "
              "at the entry's word (only the search's count could tell)");
    }

    // EVERY LINE MUST BE OF THE KIND THE ENTRY SAYS IT WAS PLACED AS.
    {
        Module m;
        BuildMini(m, kShift);
        const Image& image = m.Finish();
        (void)RunMini(image, 0, false, nullptr, found, report);
        Entry entry;
        Check(MakeEntry(entry, "halo3.dll", "control-update", 1, report), "a searched drive as an entry");
        struct Odd
        {
            uint32_t line;
            uint8_t placed;
        };
        const Odd odds[] = {{0, 'O'}, {0, 'S'}, {1, 'U'}, {1, 'D'}, {2, 'X'}, {3, 'U'}, {3, 'S'}, {0, 0}, {1, '?'}};
        bool allRefused = true;
        for (const Odd& odd : odds)
        {
            Entry bent = entry;
            bent.placed[odd.line] = odd.placed;
            out = RunMini(image, 0, false, &bent, again, second);
            allRefused = allRefused && out.rung == Rung::Search && out.savedRefused == kSavedStale && again.ok;
        }
        Check(allRefused, "a signature saved as an in-function row, an in-function row saved as a signature, a derived "
                          "address saved as a searched one, a line with no placement: none is accepted");
    }
}

// THE SAVED ADDRESSES ARE ASKED FOR ONLY WHEN THEY ARE NEEDED. Asking is opening a file, which on a synced or a
// network folder can keep the worker waiting: a drive that verifies at the recorded addresses must not ask.
struct Asked
{
    const drive_known::Entry* entry = nullptr;
    const char* refuse = nullptr;
    unsigned times = 0;
};
const drive_known::Entry* AskSaved(void* raw, const char*& refused)
{
    auto* const asked = static_cast<Asked*>(raw);
    ++asked->times;
    refused = asked->refuse;
    return asked->entry;
}

void TestSavedAskedOnlyWhenNeeded()
{
    using drive_known::Rung;
    Module m;
    BuildMini(m, 0);
    const Image& image = m.Finish();
    const auto plan = [](Resolver& r) { return MiniPlan(r); };
    Mini found;
    Report report;
    Asked asked;
    drive_known::Earlier earlier;
    earlier.look = &AskSaved;
    earlier.lookContext = &asked;

    drive_known::Outcome out = drive_known::RunTyped(image, 0, true, earlier, plan, found, report);
    Check(out.rung == Rung::Recorded && found.ok && out.scans == 0 && asked.times == 0 && !out.savedLooked,
          "on the recorded build with everything verifying, the saved addresses are never asked for");
    out = drive_known::RunTyped(image, 0, false, earlier, plan, found, report);
    Check(out.rung == Rung::Search && found.ok && asked.times == 1 && out.savedLooked && !out.savedTried &&
              !out.savedRefused,
          "on a build that is not the recorded one they are asked for once; none held, so the drive is searched");
    drive_known::Entry entry;
    Check(drive_known::MakeEntry(entry, "halo3.dll", "control-update", 1, report), "what that search found, as an entry");
    asked = Asked{&entry};
    out = drive_known::RunTyped(image, 0, false, earlier, plan, found, report);
    Check(out.rung == Rung::Saved && out.scans == 0 && asked.times == 1 && found.ok,
          "with an entry held they are asked for once, and verified with no search");

    // The recorded build with one line that does not verify: rung 1 (a) fails first, and only then are they asked for.
    Module moved;
    BuildMini(moved, 0, 0x70);
    asked = Asked{};
    out = drive_known::RunTyped(moved.Finish(), 0, true, earlier, plan, found, report);
    Check(out.recordedTried && out.rung == Rung::Search && asked.times == 1 && out.savedLooked && found.ok,
          "on the recorded build they are asked for only once the recorded addresses have failed to verify");
    // What the cache refused is what is reported.
    asked = Asked{nullptr, drive_known::kOtherBuild};
    out = drive_known::RunTyped(image, 0, false, earlier, plan, found, report);
    Check(out.rung == Rung::Search && asked.times == 1 && !out.savedTried && out.savedRefused == drive_known::kOtherBuild,
          "an entry the cache holds and may not hand over is reported with its reason");
    // A forced stale save stands for saved addresses that are wrong: the cache is not asked for the real ones.
    asked = Asked{&entry};
    out = drive_known::RunTyped(image, kFaultSavedStale, true, earlier, plan, found, report);
    Check(out.rung == Rung::Search && asked.times == 0 && out.savedTried && out.savedRefused == drive_known::kSavedStale,
          "fault 0x40 does not ask the cache");
}

// WHAT THE PROCESS ALREADY RESOLVED IN THIS MODULE. A drive is installed again at every level; the second install
// of the same image must search nothing, whatever the first came to.
constexpr char kAbsent[] = "0F 1F 84 00 11 22 33 44 55 66 77 88 99 AA BB C0";   // stands in no test module

// The Mini drive with one line more, which it can do without.
Mini MiniPlanWithOption(Resolver& r)
{
    const Mini f = MiniPlan(r);
    (void)r.Unique("option", 0x2800, kAbsent);
    return f;
}

void TestRemembered()
{
    using drive_known::Earlier;
    using drive_known::Rung;
    constexpr uint32_t kShift = 0x340;
    const auto plan = [](Resolver& r) { return MiniPlan(r); };
    const auto withOption = [](Resolver& r) { return MiniPlanWithOption(r); };
    Mini first, again;
    Report report, second;
    drive_known::Outcome out;

    // COMPLETE.
    {
        Module m;
        BuildMini(m, kShift);
        const Image& image = m.Finish();
        out = drive_known::RunTyped(image, 0, false, Earlier{}, plan, first, report);
        Check(out.rung == Rung::Search && out.moduleScans == 2 && first.ok && report.Complete(),
              "the first install of a drive in a module: searched, two scans of the module");
        Earlier earlier;
        earlier.remembered = &report;
        out = drive_known::RunTyped(image, 0, false, earlier, plan, again, second);
        Check(out.rung == Rung::Remembered && out.rememberedTried && out.scans == 0 && out.moduleScans == 0 &&
                  !out.savedLooked && !out.learn,
              "COMPLETE: the second install of the same image does zero scans, and asks for no saved addresses");
        Check(again.ok && again.entry == first.entry && again.field == first.field && again.call == first.call &&
                  drive_known::SameReport(second, report) && second.Fingerprint() == report.Fingerprint(),
              "and comes to the same result, address for address, read the same way");

        // Under a forced fault what was remembered is not used: a fault's result is not the module's.
        out = drive_known::RunTyped(image, kFaultRecordWrong, false, earlier, plan, again, second);
        Check(!out.rememberedTried && out.rung == Rung::Search && again.ok && out.moduleScans == 2,
              "under a forced fault what was remembered is not used");
        out = drive_known::RunTyped(image, kFaultSearchToo, false, earlier, plan, again, second);
        Check(!out.rememberedTried && out.rung == Rung::Search, "nor under the self-test bit");

        // The same bytes elsewhere: what was remembered does not hold, and the drive is resolved afresh.
        Module other;
        BuildMini(other, 0x400);
        out = drive_known::RunTyped(other.Finish(), 0, false, earlier, plan, again, second);
        Check(out.rememberedTried && out.rung == Rung::Search && again.ok && again.entry == kMiniEntry + 0x400 &&
                  out.moduleScans == 2,
              "bytes that no longer stand where they were remembered: it is void, and the drive is resolved afresh");
        // A plan that asks for other lines than were remembered.
        out = drive_known::RunTyped(image, 0, false, earlier, withOption, again, second);
        Check(out.rememberedTried && out.rung == Rung::Search && again.ok && second.count == 5,
              "a plan that asks for other lines than were remembered: resolved afresh");
    }

    // AN OPTIONAL LINE MISSING.
    {
        Module m;
        BuildMini(m, kShift);
        out = drive_known::RunTyped(m.Finish(), 0, false, Earlier{}, withOption, first, report);
        Check(out.rung == Rung::Search && out.moduleScans == 3 && first.ok && !report.Complete() &&
                  report.NotFound() == 1 && !out.learn,
              "a drive with a line it can do without, which is not in the module: usable, never whole, nothing to save");
        Earlier earlier;
        earlier.remembered = &report;
        out = drive_known::RunTyped(m.Finish(), 0, false, earlier, withOption, again, second);
        Check(out.rung == Rung::Remembered && out.scans == 0 && out.moduleScans == 0 && again.ok &&
                  again.entry == first.entry && drive_known::SameReport(second, report) &&
                  second.lines[4].verdict == Verdict::Missing,
              "AN OPTIONAL LINE MISSING: the second install does zero scans; the line that was not found is not "
              "searched for again");
        // ... not even where it would now be found: what is handed over as remembered is repeated, not looked
        // into. (Whether to hand it over is its owner's: a result that is not whole only after the module has been
        // searched afresh a few times, TestRepeatRemembered; and nothing at all for another module instance.)
        m.Put(0x2800, kAbsent);
        out = drive_known::RunTyped(m.Finish(), 0, false, earlier, withOption, again, second);
        Check(out.rung == Rung::Remembered && out.scans == 0 && second.lines[4].verdict == Verdict::Missing,
              "what was not found in this module is not looked for again in it");
        out = drive_known::RunTyped(m.Finish(), 0, false, Earlier{}, withOption, again, second);
        Check(out.rung == Rung::Search && second.Complete() && second.lines[4].found == 0x2800,
              "with nothing remembered (another module instance) it is searched for, and found");
    }

    // REFUSED: a mandatory line is not in the module.
    {
        Module m;
        PutFunction(m, kMiniEntry + kShift);   // the entry and its field; no call site anywhere
        const Image& image = m.Finish();
        out = drive_known::RunTyped(image, 0, false, Earlier{}, plan, first, report);
        Check(out.rung == Rung::Search && out.moduleScans == 2 && !first.ok && !first.call && first.entry &&
                  report.NotFound() == 2,
              "a drive whose mandatory line is not in the module: refused, after two scans of the module");
        Earlier earlier;
        earlier.remembered = &report;
        out = drive_known::RunTyped(image, 0, false, earlier, plan, again, second);
        Check(out.rung == Rung::Remembered && out.scans == 0 && out.moduleScans == 0 && !again.ok && !again.call &&
                  again.entry == first.entry && drive_known::SameReport(second, report) &&
                  second.lines[2].verdict == Verdict::Missing && !std::strcmp(second.lines[2].name, "call site"),
              "REFUSED: the second install does zero scans, and is refused again by the same line");
    }

    // ON THE RECORDED BUILD rung 1 (a) comes first and needs nothing remembered; where it fails, what was
    // remembered serves.
    {
        Module m;
        BuildMini(m, 0);
        (void)drive_known::RunTyped(m.Finish(), 0, false, Earlier{}, plan, first, report);
        Earlier earlier;
        earlier.remembered = &report;
        out = drive_known::RunTyped(m.Finish(), 0, true, earlier, plan, again, second);
        Check(out.rung == Rung::Recorded && !out.rememberedTried && out.scans == 0 && again.ok,
              "on the recorded build the recorded addresses are verified first, and nothing remembered is needed");
        Module moved;
        BuildMini(moved, 0, 0x70);   // the field is not at its recorded offset: rung 1 (a) fails
        out = drive_known::RunTyped(moved.Finish(), 0, true, Earlier{}, plan, first, report);
        Check(out.recordedTried && out.rung == Rung::Search && out.scans == 3 && first.ok,
              "the recorded build with one line that does not verify there: searched");
        earlier.remembered = &report;
        out = drive_known::RunTyped(moved.Finish(), 0, true, earlier, plan, again, second);
        Check(out.recordedTried && out.rung == Rung::Remembered && out.scans == 0 && out.moduleScans == 0 && again.ok &&
                  again.field == first.field && second.lines[1].how == How::FunctionExtent,
              "and its second install does zero scans: rung 1 (a) fails as before, and what was remembered holds");
    }
}

// A RESULT THAT IS NOT WHOLE IS NOT BELIEVED FROM ONE SEARCH: when what is remembered is repeated, and when the
// drive is resolved afresh. (The repeating itself is above; the counting is the DLL's, tests/drive_cascade_tests.)
void TestRepeatRemembered()
{
    Module m;
    BuildMini(m, 0x340);
    const auto plan = [](Resolver& r) { return MiniPlan(r); };
    const auto withOption = [](Resolver& r) { return MiniPlanWithOption(r); };
    Mini found;
    Report whole, missing;
    (void)drive_known::RunTyped(m.Finish(), 0, false, drive_known::Earlier{}, plan, found, whole);
    (void)drive_known::RunTyped(m.Finish(), 0, false, drive_known::Earlier{}, withOption, found, missing);
    Check(whole.Complete() && !missing.Complete(), "a whole result, and one with a line missing");
    Check(drive_known::kFreshSearches == 3, "a result that is not whole is searched for afresh three times");
    Check(drive_known::RepeatRemembered(whole, 0) && drive_known::RepeatRemembered(whole, 7),
          "a whole result is repeated from the first: it is verified in place");
    Check(!drive_known::RepeatRemembered(missing, 0) && !drive_known::RepeatRemembered(missing, 1) &&
              !drive_known::RepeatRemembered(missing, 2),
          "a result that is not whole is not repeated while fresh searches remain: the drive is searched for again");
    Check(drive_known::RepeatRemembered(missing, 3) && drive_known::RepeatRemembered(missing, 4),
          "after the last of them the miss is the module's own, and is repeated");
}

// A PLAN THAT ASKS FOR MORE LINES THAN A REPORT HOLDS is refused, the same on every rung, and nothing of it is
// kept. (No real plan is near it: tests/drive_sites_tests.cpp holds each to three quarters of a report.)
struct Long
{
    uint32_t found = 0;   // how many of its lines came back with an address
};
Long LongPlan(Resolver& r, uint32_t lines)
{
    Long f;
    for (uint32_t i = 0; i < lines; ++i)
        f.found += r.Unique("entry", kMiniEntry, kEntry) ? 1u : 0u;
    return f;
}

void TestPlanTooLong()
{
    using drive_known::Earlier;
    using drive_known::Rung;
    Module m;
    PutFunction(m, kMiniEntry);
    const Image& image = m.Finish();
    const auto fits = [](Resolver& r) { return LongPlan(r, kMaxLines); };
    const auto tooLong = [](Resolver& r) { return LongPlan(r, kMaxLines + 1); };
    Long found;
    Report report, searched;
    drive_known::Outcome out = drive_known::RunTyped(image, 0, true, Earlier{}, fits, found, report);
    Check(out.rung == Rung::Recorded && !out.refused && found.found == kMaxLines && report.Complete(),
          "a plan of exactly as many lines as a report holds is whole");
    out = drive_known::RunTyped(image, 0, false, Earlier{}, fits, found, searched);
    drive_known::Entry entry;
    Check(out.rung == Rung::Search && searched.Complete() &&
              drive_known::MakeEntry(entry, "halo3.dll", "control-update", 1, searched),
          "by search too, and it can be saved");

    struct Case
    {
        const char* rung;
        bool recordedBuild;
        const drive_known::Entry* saved;
        const Report* remembered;
    };
    const Case cases[] = {{"at the recorded addresses", true, nullptr, nullptr},
                          {"from what was remembered", false, nullptr, &searched},
                          {"at saved addresses", false, &entry, nullptr},
                          {"by search", false, nullptr, nullptr}};
    for (const Case& c : cases)
    {
        Earlier earlier;
        earlier.saved = c.saved;
        earlier.remembered = c.remembered;
        found.found = 4242;   // nothing must be kept: this is still here afterwards
        out = drive_known::RunTyped(image, 0, c.recordedBuild, earlier, tooLong, found, report);
        char what[160];
        std::snprintf(what, sizeof(what), "one line more than a report holds, %s: refused whole with its own reason, "
                                          "nothing kept, nothing to save", c.rung);
        Check(out.rung == Rung::None && out.refused == drive_known::kTooManyLines && !out.learn &&
                  found.found == 4242 && report.dropped == 1 && report.count == kMaxLines && !report.Complete(),
              what);
    }
}

void TestRungFaults()
{
    Module m;
    BuildMini(m, 0);
    const Image& image = m.Finish();
    Mini found;
    Report report;
    drive_known::Outcome out = RunMini(image, kFaultRecordWrong, true, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Search && out.recordedTried && found.ok && found.entry == kMiniEntry &&
              report.Count(Verdict::Moved) == 4 && out.scans == 2,
          "fault 0x04, the whole path: the recorded addresses do not verify, the search finds them, MOVED is reported");
    out = RunMini(image, kFaultSignatureMissing, true, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Search && out.recordedTried && !found.ok && !out.learn,
          "fault 0x01 fails the known addresses too, so the search shows it: nothing found, nothing learned");
    out = RunMini(image, kFaultSignatureAmbiguous, true, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Search && found.ok && report.lines[0].verdict == Verdict::RecordAmongSeveral,
          "fault 0x02 fails the known addresses too, so the search shows its tie-break");
    out = RunMini(image, kFaultOffsetMoved, true, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Search && found.ok && report.lines[1].how == How::FunctionExtent,
          "fault 0x20 fails the known offset, so the search inside the function shows");
    out = RunMini(image, kFaultSavedStale, true, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Search && !out.recordedTried && out.savedTried &&
              out.savedRefused == drive_known::kSavedStale && found.ok && out.scans == 2,
          "fault 0x40: saved addresses that are stale do not verify, and the search finds everything");
    out = RunMini(image, kFaultSearchToo, true, nullptr, found, report);
    Check(out.rung == drive_known::Rung::Recorded && out.searchedToo && out.searchAgrees && out.scans == 2 && found.ok &&
              AllHow(report, How::Known),
          "self-test 0x80: the search is run beside a verified result, agrees, and changes nothing that is used");
    out = RunMini(image, 0, true, nullptr, found, report);
    Check(!out.searchedToo && out.scans == 0, "without it nothing is searched");
    // The self-test beside SAVED addresses (a build that is not the recorded one).
    Report searched;
    (void)RunMini(image, 0, false, nullptr, found, searched);
    drive_known::Entry entry;
    Check(drive_known::MakeEntry(entry, "halo3.dll", "control-update", 1, searched), "what a search found, as an entry");
    out = RunMini(image, kFaultSearchToo, false, &entry, found, report);
    Check(out.rung == drive_known::Rung::Saved && out.searchedToo && out.searchAgrees && out.scans == 2 && found.ok &&
              AllHow(report, How::Known),
          "self-test 0x80 beside saved addresses: they stay in use, and the search chooses the same for every line");
}

// `text` with its one `from` replaced by `to`.
std::string Swap(const std::string& text, const char* from, const char* to)
{
    const size_t at = text.find(from);
    Check(at != std::string::npos && text.find(from, at + 1) == std::string::npos, "a test edit finds its one place");
    return at == std::string::npos ? text : text.substr(0, at) + to + text.substr(at + std::strlen(from));
}

// A file that holds just this entry, written as the mod writes it: its checksum is its own, whatever it holds.
std::string FileOf(const drive_known::Entry& entry)
{
    static drive_known::File file;
    static char text[drive_known::kMaxFileText];
    file = drive_known::File{};
    file.entries[0] = entry;
    file.count = 1;
    return std::string(text, drive_known::Format(file, text, sizeof(text)));
}

uint32_t EntriesIn(const std::string& text)
{
    static drive_known::File file;
    drive_known::Parse(text.data(), text.size(), file);
    return file.count;
}

void TestCacheText()
{
    using namespace drive_known;
    Module a, b;
    BuildMini(a, 0x340);
    BuildMini(b, 0x80);
    Mini found;
    Report first, second;
    (void)RunMini(a.Finish(), 0, false, nullptr, found, first);
    (void)RunMini(b.Finish(), 0, false, nullptr, found, second);
    Entry halo3, halo4;
    Check(MakeEntry(halo3, ModuleName(GameTitle::Halo3), "control-update", 0x68A0EDFC04768000ull, first) &&
              MakeEntry(halo4, ModuleName(GameTitle::Halo4), "pilot-controller", 0x68A0E7BF04A3F000ull, second),
          "two titles' drives as entries");
    File file;
    Put(file, halo3);
    Put(file, halo4);
    Check(file.count == 2, "both are held");
    static char text[kMaxFileText];
    const size_t length = Format(file, text, sizeof(text));
    char rows[64];
    std::snprintf(rows, sizeof(rows), " %u 4 U1540,O15A0,U2340,D2348 ", kRulesVersion);
    Check(length > 0 && std::strstr(text, "NOT a setting") && std::strstr(text, "Safe to DELETE") &&
              std::strstr(text, "ends in a checksum") && std::strstr(text, "schema=2\r\n") &&
              std::strstr(text, "entry halo3.dll 68A0EDFC04768000 control-update ") && std::strstr(text, rows),
          "the file's text: a head that says what it is, the schema, and a line per entry with the search's rules "
          "and, for every address, how it was placed");
    File back;
    Parse(text, length, back);
    const Entry* h3 = Find(back, "halo3.dll", "control-update");
    const Entry* h4 = Find(back, "halo4.dll", "pilot-controller");
    Check(back.count == 2 && h3 && h4 && h3->stamp == halo3.stamp && h3->plan == halo3.plan && h3->count == 4 &&
              h3->rules == kRulesVersion && !std::memcmp(h3->rvas, halo3.rvas, sizeof(uint32_t) * 4) &&
              !std::memcmp(h3->placed, halo3.placed, 4) && Checksum(*h3) == Checksum(halo3) &&
              h4->stamp == halo4.stamp && h4->rvas[0] == kMiniEntry + 0x80,
          "read back whole: two titles' entries coexist");
    Check(!Find(back, "halo3.dll", "pilot-controller") && !Find(back, "haloreach.dll", "control-update"),
          "a drive that was never saved has no entry");

    // WHICH ENTRY MAY BE TRIED: one of this build of the module, found under the rules the search has now.
    const char* refused = "?";
    Check(h3 && Usable(back, "halo3.dll", "control-update", halo3.stamp, refused) == h3 && !refused,
          "an entry of this build of the DLL, found under these rules, may be tried");
    Check(!Usable(back, "halo3.dll", "control-update", halo3.stamp ^ 0x100000000ull, refused) && refused == kOtherBuild,
          "one of another build of the DLL may not, and the reason is given");
    Entry aged = halo3;
    ++aged.rules;
    const std::string agedText = FileOf(aged);
    File agedFile;
    Parse(agedText.data(), agedText.size(), agedFile);
    Check(agedFile.count == 1 && agedFile.entries[0].rules == kRulesVersion + 1 &&
              !Usable(agedFile, "halo3.dll", "control-update", halo3.stamp, refused) && refused == kOtherRules,
          "AN ENTRY UNDER ANOTHER RULES VERSION IS IGNORED: whole and well-formed as it is, it may not be tried");
    Check(!Usable(back, "haloreach.dll", "control-update", halo3.stamp, refused) && !refused,
          "a drive with no entry: nothing to try, and nothing refused");

    // A DAMAGED CHECKSUM IS IGNORED. One character of an entry changed, anywhere in it, and that entry is not
    // read; the other title's still is.
    {
        const std::string whole(text, length);
        const size_t line = whole.find("entry halo3.dll ");
        const size_t lineEnd = whole.find("\r\n", line);
        const size_t rowsAt = whole.find(rows, line);
        struct Damage
        {
            size_t at;
            const char* what;
        };
        const Damage damages[] = {
            {line + 16, "the stamp"},
            {whole.find(" control-update ", line) + 16, "the plan"},
            {rowsAt + 1, "the rules"},
            {whole.find(" 4 U1540", line) + 1, "the count"},
            {whole.find("U1540", line) + 1, "an address"},
            {whole.find(",O15A0", line) + 1, "how a line was placed"},
            {lineEnd - 1, "the checksum itself"},
        };
        bool allDropped = line != std::string::npos && rowsAt != std::string::npos && lineEnd != std::string::npos;
        for (const Damage& damage : damages)
        {
            std::string damaged = whole;
            char& c = damaged[damage.at];
            c = c == 'O' ? 'X' : c == '1' ? '2' : '1';
            File got;
            Parse(damaged.data(), damaged.size(), got);
            const bool dropped = damaged != whole && got.count == 1 && !Find(got, "halo3.dll", "control-update") &&
                                 Find(got, "halo4.dll", "pilot-controller");
            if (!dropped)
                std::printf("      still read with %s changed\n", damage.what);
            allDropped = allDropped && dropped;
        }
        Check(allDropped, "A DAMAGED CHECKSUM IS IGNORED: with one character changed in its stamp, its plan, its rules, "
                          "its count, an address, a placement or the checksum itself, an entry is not read, and the "
                          "other title's still is");
    }

    // One title's drive is saved again (another build): its entry is replaced, the other title's is kept.
    Entry newer = halo3;
    newer.stamp = 0x7000000004800000ull;
    newer.rvas[0] = 0x9999;
    Put(back, newer);
    Check(back.count == 2 && Find(back, "halo3.dll", "control-update")->stamp == newer.stamp &&
              Find(back, "halo4.dll", "pilot-controller")->rvas[0] == kMiniEntry + 0x80,
          "saving one drive again replaces its entry and keeps every other");

    // What is not a whole, well-formed file of this schema holds nothing, or only its whole entries.
    File none;
    Parse(nullptr, 0, none);
    Check(none.count == 0, "no text: nothing");
    Parse(text, length - 2, none);   // the last line lost its newline: something cut the file short
    Check(none.count == 1 && Find(none, "halo3.dll", "control-update") && !Find(none, "halo4.dll", "pilot-controller"),
          "a file cut short keeps its whole entries and drops the cut one");
    Parse(text, length / 2, none);
    Check(none.count <= 1, "cut in the middle: at most the entries that are whole");

    // Written as the mod writes an entry, checksum and all, and still not one: each is refused on its own ground,
    // not by its checksum.
    Entry zeroStamp = halo3, zeroAddress = halo3, noLines = halo3;
    zeroStamp.stamp = 0;
    zeroAddress.rvas[2] = 0;
    noLines.count = 0;
    Check(EntriesIn(FileOf(halo3)) == 1 && EntriesIn(FileOf(zeroStamp)) == 0 && EntriesIn(FileOf(zeroAddress)) == 0 &&
              EntriesIn(FileOf(noLines)) == 0,
          "a zero stamp, a zero address, no lines at all: not an entry, though its checksum is its own");
    // Text that is not an entry as the mod writes one.
    const std::string good = FileOf(halo3);
    const std::string edits[] = {
        good.substr(good.find("entry halo3.dll")),                                     // no schema line above it
        Swap(good, "schema=2", "schema=1"),                                   // the schema before this one
        Swap(good, "schema=2", "schema=3"),
        Swap(good, " 4 U1540", " 5 U1540"),                                   // more lines promised than there are
        Swap(good, " 4 U1540", " 3 U1540"),                                   // fewer
        Swap(good, ",O15A0,", ",,"),                                          // a line with nothing in it
        Swap(good, ",O15A0,", ",15A0,"),                                      // an address with no placement
        Swap(good, ",O15A0,", ",Q15A0,"),                                     // a placement that is none
        Swap(good, "D2348", "D123456789"),                                    // an address that is not 32 bits
        Swap(good, "D2348 ", "D2348 x "),                                     // something before the checksum
        good.substr(0, good.size() - 2) + " x\r\n",                           // something after it
        good.substr(0, good.size() - 19) + "\r\n",                            // no checksum
        Swap(good, "halo3.dll", "a-module-name-that-is-far-too-long.dll"),
        Swap(good, "68A0EDFC04768000", "zz"),
        "schema=2\r\nentry halo3.dll 68A0EDFC04768000\r\n",
        "schema=2\r\nentry\r\n",
        "schema=2\r\nsomething else entirely\r\n\r\n\x01\x02\x03\r\n",
        "\xFF\xFEschema=2",
    };
    bool allIgnored = true;
    for (const std::string& one : edits)
    {
        const bool ignored = EntriesIn(one) == 0;
        if (!ignored)
            std::printf("      read as an entry: %s\n", one.c_str());
        allIgnored = allIgnored && ignored;
    }
    Check(allIgnored, "no schema, another schema, a wrong count, an empty, unplaced, misplaced or oversized address, text "
                      "around the checksum, no checksum, an overlong name, a stamp that is no number, a short line, "
                      "garbage: each is ignored");
    // A good entry among bad lines, with a bare line feed for its ending.
    Entry halo2;
    std::snprintf(halo2.module, sizeof(halo2.module), "halo2.dll");
    std::snprintf(halo2.id, sizeof(halo2.id), "control-update");
    halo2.stamp = 0x68A0F0F202A38000ull;
    halo2.plan = 0xAB;
    halo2.rules = kRulesVersion;
    halo2.count = 2;
    halo2.rvas[0] = 0x6C3DF0;
    halo2.rvas[1] = 0x6C3E5E;
    halo2.placed[0] = 'U';
    halo2.placed[1] = 'O';
    std::string halo2Line = FileOf(halo2);
    halo2Line = halo2Line.substr(halo2Line.find("entry halo2.dll"));
    halo2Line.erase(halo2Line.size() - 2, 1);   // its "\r\n" becomes "\n"
    const std::string mixed = "# a comment\r\nschema=2\r\nnot an entry\r\n" + halo2Line + "entry halo2.dll broken\r\n";
    Parse(mixed.data(), mixed.size(), none);
    Check(none.count == 1 && none.entries[0].count == 2 && none.entries[0].rvas[1] == 0x6C3E5E &&
              none.entries[0].plan == 0xAB && none.entries[0].placed[1] == 'O',
          "a good entry among bad lines is taken (with either line ending)");

    // Limits.
    char small[64];
    Check(Format(file, small, sizeof(small)) == 0 && Format(file, nullptr, 0) == 0, "text that does not fit is not written");
    // A FULL FILE: what is put is never refused. One entry gives way, the oldest that is surely dead first.
    static File full;
    const auto fill = [&halo3]() {
        full = File{};
        for (uint32_t i = 0; i < kMaxEntries; ++i)
        {
            Entry e = halo3;
            std::snprintf(e.id, sizeof(e.id), "drive-%u", i);
            Put(full, e);
        }
    };
    Entry extra = halo3;
    std::snprintf(extra.id, sizeof(extra.id), "one-more");
    fill();
    Check(full.count == kMaxEntries && Format(full, text, sizeof(text)) > 0, "sixteen entries are held, and their text fits");
    full.entries[5].stamp ^= 1;   // one of them is of another build of the module
    full.entries[11].stamp ^= 1;  // and a later one too
    Put(full, extra);
    Check(full.count == kMaxEntries && !Find(full, "halo3.dll", "drive-5") && Find(full, "halo3.dll", "drive-11") &&
              Find(full, "halo3.dll", "one-more") == &full.entries[kMaxEntries - 1] &&
              Find(full, "halo3.dll", "drive-0") == &full.entries[0] && Find(full, "halo3.dll", "drive-6") == &full.entries[5],
          "a full file: the oldest entry of another build of the module gives way, and every other is kept, in order");
    fill();
    std::snprintf(full.entries[2].module, sizeof(full.entries[2].module), "halo4.dll");
    std::snprintf(full.entries[9].module, sizeof(full.entries[9].module), "halo4.dll");
    Put(full, extra);
    Check(full.count == kMaxEntries && !Find(full, "halo4.dll", "drive-2") && Find(full, "halo4.dll", "drive-9") &&
              Find(full, "halo3.dll", "one-more") && Find(full, "halo3.dll", "drive-0"),
          "with none of another build in it, the oldest entry of another module gives way");
    fill();
    Put(full, extra);
    Check(full.count == kMaxEntries && !Find(full, "halo3.dll", "drive-0") &&
              Find(full, "halo3.dll", "drive-1") == &full.entries[0] && Find(full, "halo3.dll", "one-more"),
          "with nothing else to give, the oldest entry of all gives way");
    fill();
    Entry held = halo3;
    std::snprintf(held.id, sizeof(held.id), "drive-7");
    held.rvas[0] = 0x7777;
    Put(full, held);
    Check(full.count == kMaxEntries && Find(full, "halo3.dll", "drive-7") == &full.entries[7] &&
              full.entries[7].rvas[0] == 0x7777 && Find(full, "halo3.dll", "drive-0"),
          "a drive that is already held is replaced where it stands, in a full file as in any other");
    {
        // The longest entry there can be, sixteen times over.
        Entry big;
        std::memset(big.module, 'm', kMaxModule - 1);
        std::memset(big.id, 'd', kMaxId - 1);
        big.stamp = ~0ull;
        big.plan = ~0ull;
        big.rules = ~0u;
        big.count = kMaxLines;
        for (uint32_t i = 0; i < kMaxLines; ++i)
        {
            big.rvas[i] = 0xFFFFFFFFu - i;
            big.placed[i] = 'U';
        }
        static File biggest, biggestBack;
        biggest = File{};
        for (uint32_t i = 0; i < kMaxEntries; ++i)
        {
            biggest.entries[i] = big;
            biggest.entries[i].id[0] = static_cast<char>('A' + i);
        }
        biggest.count = kMaxEntries;
        const size_t bigLength = Format(biggest, text, sizeof(text));
        Parse(text, bigLength, biggestBack);
        Check(bigLength > 0 && biggestBack.count == kMaxEntries && biggestBack.entries[kMaxEntries - 1].count == kMaxLines &&
                  biggestBack.entries[0].rvas[kMaxLines - 1] == 0xFFFFFFFFu - (kMaxLines - 1) &&
                  biggestBack.entries[3].rules == ~0u && biggestBack.entries[3].id[0] == 'D',
              "the longest entry there can be fits a line, and a file full of them fits the file's text");
    }
    Report incomplete = first;
    incomplete.lines[2].verdict = Verdict::Missing;
    Entry e;
    Check(!MakeEntry(e, "halo3.dll", "control-update", 1, incomplete) && !MakeEntry(e, "halo3.dll", "two words", 1, first) &&
              !MakeEntry(e, "", "control-update", 1, first) && !MakeEntry(e, "halo3.dll", "control-update", 0, first) &&
              !MakeEntry(e, "halo3.dll", nullptr, 1, first),
          "only a whole resolution, under one-word names and a real stamp, becomes an entry");
    Check(!std::strcmp(ModuleName(GameTitle::HaloReach), "haloreach.dll") && !*ModuleName(GameTitle::None),
          "each title's module by name");
}

} // namespace

int main()
{
    TestCompile();
    TestFoundAsRecorded();
    TestMovedAndFollowed();
    TestAmbiguousRefused();
    TestAmbiguousDisambiguatedByRecord();
    TestMissingRefused();
    TestCountEdges();
    TestCountAgainstBruteForce();
    TestFunctionsFromPdata();
    TestInFunction();
    TestFunctionStart();
    TestProofs();
    TestFaults();
    TestThroughOwnHooks();
    TestReadImage();
    TestReport();
    TestRungRecorded();
    TestRungOneFailsWhole();
    TestRungSaved();
    TestSavedPlacement();
    TestSavedAskedOnlyWhenNeeded();
    TestRemembered();
    TestRepeatRemembered();
    TestPlanTooLong();
    TestRungFaults();
    TestCacheText();
    std::printf("drive resolve tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
