// EVERY REAL PLAN OF drive_sites.h, RUN HERE. The plans that decide what the direct drives hook -- each title's
// control update with its caller map, the two pilots' controllers, Halo 2's accumulator, and the dev observers'
// tables -- are run against an image synthesised from the tables themselves: each row's bytes laid at its
// recorded RVA, inside function extents generated from the rows' own "where" with a matching .pdata, with the
// wildcards filled so that every call lands where the plan requires and every rip-relative load names what it
// must. Nothing here is a game byte beyond what the tables already hold.
//
// For each plan: as recorded (the search finds everything where it is recorded, and the known addresses verify
// with no scan and choose the same); moved (the same image with all its code 0x40 later: found by search, saved,
// and verified from the saved addresses with no scan); and ONE ROW BROKEN AT A TIME (that row is never found
// anywhere else, no other row wanders, the known addresses fail as a whole, and a broken hooked function means
// no hook). Then the proofs that are not rows: a caller whose call goes elsewhere, a dispatcher whose unwind
// info and prologue disagree, roles that are one call or out of order, a short anchor off its offset, a page
// that cannot be read.
//
// A control update has TWO plans: a player build's stops at the caller map, a dev build's goes on to the two tick
// anchors. They are two versions of the drive's tables, and the player build's is the one that ships: each is run
// through all of the above, and what one of them saved the other must not take.
#define DRIVE_RESOLVE_FAULTS 1
#include "drive_known.h"
#include "drive_sites.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
using namespace drive_resolve;
using drive_sites::Anchor;
using drive_sites::CallerAnchors;
using drive_sites::Find;

unsigned checks{}, failures{};
const char* g_plan = "";
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL [%s]: %s\n", g_plan, what);
    }
}

// The image a plan is run on. Code RVAs are the tables' plus `shift`; data RVAs (the TLS index, a globals slot)
// stay where the tables have them.
struct Synth
{
    struct Function
    {
        uint32_t start = 0, end = 0;   // recorded RVAs; `end`: the furthest byte a row of it reaches
        bool framed = false;           // its unwind info must give the frame its prologue gives
    };
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> laid;   // 1: a byte a row fixed (a later row's wildcard must not overwrite it)
    std::vector<Function> functions;
    std::vector<RuntimeFunction> table;
    uint32_t shift = 0, codeEnd = 0, unwindAt = 0;
    Image view{};

    Synth(uint32_t code, uint32_t imageSize, uint32_t codeShift) : shift(codeShift), codeEnd(code + 0x1000)
    {
        unwindAt = codeEnd;
        bytes.assign(std::max<size_t>(imageSize, codeEnd + 0x10000u), 0);
        std::memset(bytes.data() + 0x1000, 0xCC, codeEnd - 0x1000);
        laid.assign(codeEnd, 0);
    }

    uint32_t At(uint32_t recorded) const { return recorded + shift; }

    uint32_t Put(uint32_t recorded, const char* text)
    {
        Pattern p;
        if (!Compile(text, p) || At(recorded) + p.length > codeEnd)
        {
            Check(false, "a table's pattern compiles and fits the synthetic code");
            return 0;
        }
        for (uint32_t i = 0; i < p.length; ++i)
        {
            const uint32_t at = At(recorded) + i;
            if (p.wild[i])
            {
                if (!laid[at])
                    bytes[at] = 0xA5;
                continue;
            }
            if (laid[at] && bytes[at] != p.bytes[i])
                Check(false, "two rows that overlap agree on the bytes they share");
            bytes[at] = p.bytes[i];
            laid[at] = 1;
        }
        return p.length;
    }

    void Fix32(uint32_t at, int32_t value)
    {
        std::memcpy(bytes.data() + at, &value, sizeof(value));
        std::memset(laid.data() + at, 1, 4);
    }
    // The E8 at `recordedCall` calls the code recorded at `recordedTarget`.
    void Call(uint32_t recordedCall, uint32_t recordedTarget)
    {
        Fix32(At(recordedCall) + 1, static_cast<int32_t>(At(recordedTarget)) - static_cast<int32_t>(At(recordedCall) + 5));
    }
    // The instruction at `recordedInstruction` loads from the DATA at `target`.
    void Rip(uint32_t recordedInstruction, uint32_t displacement, uint32_t length, uint32_t target)
    {
        Fix32(At(recordedInstruction) + displacement,
              static_cast<int32_t>(target) - static_cast<int32_t>(At(recordedInstruction) + length));
    }

    Function& FunctionAt(uint32_t start)
    {
        for (Function& f : functions)
            if (f.start == start)
                return f;
        functions.push_back(Function{start, start + 1, false});
        return functions.back();
    }
    void Cover(uint32_t start, uint32_t recorded, uint32_t length)
    {
        Function& f = FunctionAt(start);
        f.end = std::max(f.end, recorded + length);
    }

    // Lays a table's rows, and gives each the function its "where" says it is in.
    template <size_t N>
    void Rows(const Anchor (&rows)[N])
    {
        for (size_t i = 0; i < N; ++i)
        {
            const Anchor& a = rows[i];
            const uint32_t length = Put(a.rva, a.pattern);
            switch (a.where.find)
            {
            case Find::Entry: Cover(a.rva, a.rva, length); break;
            case Find::Member:
                Check(rows[a.where.via].where.find == Find::Entry, "a member row's function is an entry row's");
                Cover(rows[a.where.via].rva, a.rva, length);
                break;
            case Find::Within:
                Cover(a.where.functionRva, a.rva, length);
                Cover(a.where.functionRva, rows[a.where.via].rva, PatternLength(rows[a.where.via].pattern));
                break;
            default: break;
            }
        }
    }

    const Image& Finish(uint32_t tlsIndexRva)
    {
        std::sort(functions.begin(), functions.end(), [](const Function& a, const Function& b) { return a.start < b.start; });
        table.clear();
        for (size_t i = 0; i < functions.size(); ++i)
        {
            const Function& f = functions[i];
            uint32_t end = f.end + 0x10;
            if (i + 1 < functions.size())
            {
                Check(f.end <= functions[i + 1].start, "the functions the rows describe do not overlap");
                end = std::min(end, functions[i + 1].start);
            }
            const uint32_t unwind = unwindAt;
            uint8_t info[12] = {0x01, 0x00, 0x00, 0x00};
            uint32_t size = 4;
            if (f.framed)
            {
                // One ALLOC_LARGE of the whole frame the prologue's own instructions give.
                uint32_t frame = 0;
                Check(aim_caller::FrameFromPrologue(bytes.data() + At(f.start), 64, frame), "a dispatcher's prologue decodes");
                info[1] = 4;      // SizeOfProlog
                info[2] = 3;      // three slots
                info[4] = 4;
                info[5] = 0x11;   // UWOP_ALLOC_LARGE, the 32-bit form
                std::memcpy(info + 6, &frame, 4);
                size = 12;
            }
            std::memcpy(bytes.data() + unwind, info, size);
            unwindAt += size;
            table.push_back(RuntimeFunction{At(f.start), At(end), unwind});
        }
        view = Image{};
        view.base = bytes.data();
        view.size = static_cast<uint32_t>(bytes.size());
        view.executable[0] = Range{0x1000, codeEnd - 0x1000};
        view.executableCount = 1;
        view.functions = table.data();
        view.functionCount = static_cast<uint32_t>(table.size());
        view.tlsIndexRva = tlsIndexRva;
        return view;
    }
};

// ---- what each plan needs laid beyond its rows ---------------------------------------------------------------------
void LayCallers(Synth& s, const CallerAnchors& a, uint32_t entry)
{
    uint32_t target = entry;
    if (a.accumulatorCall.pattern)
    {
        const uint32_t length = s.Put(a.accumulatorCall.rva, a.accumulatorCall.pattern);
        s.Call(a.accumulatorCall.rva + a.accumulatorCall.callOffset, entry);
        s.Cover(a.dispatcher.rva, a.dispatcher.rva, s.Put(a.dispatcher.rva, a.dispatcher.pattern));
        s.Cover(a.dispatcher.rva, a.accumulatorCall.rva, length);
        s.FunctionAt(a.dispatcher.rva).framed = true;
        target = a.dispatcher.rva;
    }
    for (const drive_sites::CallSite& site : a.sites)
        if (site.pattern)
        {
            s.Put(site.rva, site.pattern);
            s.Call(site.rva + site.callOffset, target);
        }
}

void LayTick(Synth& s, const drive_sites::TickAnchors& a)
{
    s.Put(a.getterRva, a.getter);
    s.Put(a.incRva, a.inc);
}

template <size_t N>
void LayFlight(Synth& s, const Anchor (&rows)[N], const drive_sites::FlightSites& f)
{
    s.Rows(rows);
    const uint32_t call = rows[f.callRow].rva + f.callOffset;
    s.Call(call, rows[f.entryRow].rva);
    const Anchor& dispatcher = f.callers->dispatcher;
    s.Cover(dispatcher.rva, dispatcher.rva, s.Put(dispatcher.rva, dispatcher.pattern));
    s.Cover(dispatcher.rva, call, 5);
    s.FunctionAt(dispatcher.rva).framed = true;
    s.Rip(rows[f.tlsRow].rva + f.tlsLoadOffset, f.tlsDisplacement, f.tlsLength, f.tlsIndexRva);
    for (const drive_sites::CallSite& site : f.callers->sites)
        if (site.pattern)
        {
            s.Put(site.rva, site.pattern);
            s.Call(site.rva + site.callOffset, dispatcher.rva);
        }
}

template <size_t N>
void LayT13(Synth& s, const Anchor (&rows)[N], const drive_sites::FlightSites& f, const drive_sites::T13Sites& sites)
{
    LayFlight(s, rows, f);
    const auto all = [&](const auto& group, uint32_t target) {
        for (const drive_sites::RipSite& site : group)
            s.Rip(rows[site.row].rva + site.offset, site.displacement, site.length, target);
    };
    all(sites.tlsIndex, f.tlsIndexRva);
    all(sites.pages, f.tlsIndexRva + 0x100);
    all(sites.instances, f.tlsIndexRva + 0x200);
}

void LayLegacy(Synth& s, int slot)
{
    const drive_sites::LegacyDriveSite& site = drive_sites::kLegacyDriveSites[slot];
    s.Cover(site.entryRva, site.entryRva, s.Put(site.entryRva, site.prologue));
    s.Cover(site.entryRva, site.tlsRva, s.Put(site.tlsRva, site.tlsPattern));
    s.Cover(site.entryRva, site.accRva, s.Put(site.accRva, site.accPattern));
    s.Rip(site.tlsIndexLoadRva, 2, 6, site.tlsIndexRva);
    if (slot == 2)
        s.Cover(site.entryRva, drive_sites::kHalo4RecordUnitLoadRva,
                s.Put(drive_sites::kHalo4RecordUnitLoadRva, drive_sites::kHalo4RecordUnitLoad));
    else
        s.Cover(site.entryRva, drive_sites::kLegacyPitchLimitSites[slot].rva,
                s.Put(drive_sites::kLegacyPitchLimitSites[slot].rva, drive_sites::kLegacyPitchLimitSites[slot].pattern));
    LayCallers(s, drive_sites::kLegacyCallerAnchors[slot], site.entryRva);
    LayTick(s, drive_sites::kLegacyTickAnchors[slot]);
}

// ---- the plans, as the installs run them -----------------------------------------------------------------------------
struct Result
{
    bool ok = false;     // the plan's own word that the hook may exist (the observers: that they may be made ready)
    uint32_t hook = 0;   // the function that would be hooked
};

// A control update's plan: the installs' own function (drive_sites.h). `dev`: as a dev build's install runs it,
// with the two tick anchors after the caller map; else as a player build's does.
Result PlanReachControl(Resolver& r, bool dev)
{
    const drive_sites::ReachControlPlan f =
        dev ? drive_sites::PlanReachControl<true>(r) : drive_sites::PlanReachControl<false>(r);
    return {f.control.ok, f.control.entry};
}
Result PlanLegacy(Resolver& r, int slot, bool dev)
{
    const drive_sites::LegacyControlPlan f =
        dev ? drive_sites::PlanLegacyControl<true>(r, slot) : drive_sites::PlanLegacyControl<false>(r, slot);
    return {f.control.ok, f.control.entry};
}
Result PlanReachControlPlayer(Resolver& r) { return PlanReachControl(r, false); }
Result PlanReachControlDev(Resolver& r) { return PlanReachControl(r, true); }
Result PlanHalo3Player(Resolver& r) { return PlanLegacy(r, 0, false); }
Result PlanHalo3Dev(Resolver& r) { return PlanLegacy(r, 0, true); }
Result PlanOdstPlayer(Resolver& r) { return PlanLegacy(r, 1, false); }
Result PlanOdstDev(Resolver& r) { return PlanLegacy(r, 1, true); }
Result PlanHalo4Player(Resolver& r) { return PlanLegacy(r, 2, false); }
Result PlanHalo4Dev(Resolver& r) { return PlanLegacy(r, 2, true); }
Result PlanReachSabre(Resolver& r)
{
    const drive_sites::ReachSabre f = drive_sites::ResolveReachSabre(r);
    return {f.flight.ok, f.flight.entry};
}
Result PlanHalo4Flight(Resolver& r)
{
    const drive_sites::Halo4Flight f = drive_sites::ResolveHalo4Flight(r);
    return {f.flight.ok, f.flight.entry};
}
Result PlanHalo2(Resolver& r)
{
    const drive_sites::Halo2Control f = drive_sites::ResolveHalo2Control(r);
    return {f.ok, f.entry};
}
Result PlanT13Reach(Resolver& r)
{
    uint32_t found[drive_sites::kT13ReachRows]{};
    const drive_sites::T13 f = drive_sites::ResolveT13(r, drive_sites::kT13ReachAnchors, found, drive_sites::kT13ReachFlight,
                                                       drive_sites::kT13ReachSites);
    return {f.ok, f.flight.entry};
}
Result PlanT13Halo4(Resolver& r)
{
    uint32_t found[drive_sites::kT13Halo4Rows]{};
    const drive_sites::T13 f = drive_sites::ResolveT13(r, drive_sites::kT13Halo4Anchors, found, drive_sites::kT13Halo4Flight,
                                                       drive_sites::kT13Halo4Sites);
    return {f.ok, f.flight.entry};
}

void BuildReachControl(Synth& s)
{
    s.Rows(drive_sites::kReachControlAnchors);
    s.Rip(drive_sites::kReachControlAnchors[1].rva, 2, 6, drive_sites::kReachTlsIndexRva);
    LayCallers(s, drive_sites::kReachCallerAnchors, drive_sites::kReachControlAnchors[0].rva);
    LayTick(s, drive_sites::kReachTickAnchors);
}
void BuildHalo3(Synth& s) { LayLegacy(s, 0); }
void BuildOdst(Synth& s) { LayLegacy(s, 1); }
void BuildHalo4(Synth& s) { LayLegacy(s, 2); }
void BuildReachSabre(Synth& s) { LayFlight(s, drive_sites::kReachSabreAnchors, drive_sites::kReachSabreSites); }
void BuildHalo4Flight(Synth& s) { LayFlight(s, drive_sites::kHalo4FlightAnchors, drive_sites::kHalo4FlightSites); }
void BuildHalo2(Synth& s)
{
    const auto& rows = drive_sites::kHalo2ControlAnchors;
    s.Rows(rows);
    s.Call(rows[4].rva + drive_sites::kHalo2ControlCallOffset, rows[0].rva);
    s.Rip(rows[0].rva + drive_sites::kHalo2ControlSlotLoadOffset, 3, 7, drive_sites::kHalo2ControlSlotRva);
}
void BuildT13Reach(Synth& s)
{
    LayT13(s, drive_sites::kT13ReachAnchors, drive_sites::kT13ReachFlight, drive_sites::kT13ReachSites);
}
void BuildT13Halo4(Synth& s)
{
    LayT13(s, drive_sites::kT13Halo4Anchors, drive_sites::kT13Halo4Flight, drive_sites::kT13Halo4Sites);
}

struct Plan
{
    const char* name;
    Result (*run)(Resolver&);
    void (*build)(Synth&);
    uint32_t code;       // the synthetic code reaches this far
    uint32_t tlsIndex;   // 0: the drive reads none
    bool player;         // a plan a player build runs (the rest: a dev build's version of one, and the dev observers)
};

const Plan kPlans[] = {
    {"Halo 2 control update", &PlanHalo2, &BuildHalo2, 0x6D0000, 0, true},
    {"Halo 3 control update", &PlanHalo3Player, &BuildHalo3, 0x380000, drive_sites::kLegacyDriveSites[0].tlsIndexRva, true},
    {"ODST control update", &PlanOdstPlayer, &BuildOdst, 0x3C0000, drive_sites::kLegacyDriveSites[1].tlsIndexRva, true},
    {"Reach control update", &PlanReachControlPlayer, &BuildReachControl, 0x70000, drive_sites::kReachTlsIndexRva, true},
    {"Reach Sabre pilot controller", &PlanReachSabre, &BuildReachSabre, 0x530000, drive_sites::kReachTlsIndexRva, true},
    {"Halo 4 control update", &PlanHalo4Player, &BuildHalo4, 0xB0000, drive_sites::kLegacyDriveSites[2].tlsIndexRva, true},
    {"Halo 4 Broadsword pilot controller", &PlanHalo4Flight, &BuildHalo4Flight, 0x6D0000,
     drive_sites::kLegacyDriveSites[2].tlsIndexRva, true},
    {"Halo 3 control update (dev build)", &PlanHalo3Dev, &BuildHalo3, 0x380000,
     drive_sites::kLegacyDriveSites[0].tlsIndexRva, false},
    {"ODST control update (dev build)", &PlanOdstDev, &BuildOdst, 0x3C0000, drive_sites::kLegacyDriveSites[1].tlsIndexRva,
     false},
    {"Reach control update (dev build)", &PlanReachControlDev, &BuildReachControl, 0x70000,
     drive_sites::kReachTlsIndexRva, false},
    {"Halo 4 control update (dev build)", &PlanHalo4Dev, &BuildHalo4, 0xB0000,
     drive_sites::kLegacyDriveSites[2].tlsIndexRva, false},
    {"Reach pilot observer (dev)", &PlanT13Reach, &BuildT13Reach, 0x530000, drive_sites::kReachTlsIndexRva, false},
    {"Halo 4 pilot observer (dev)", &PlanT13Halo4, &BuildT13Halo4, 0x6D0000, drive_sites::kLegacyDriveSites[2].tlsIndexRva,
     false},
};

uint32_t ImageSize(const Plan& plan)
{
    return std::max<uint32_t>(plan.tlsIndex + 0x1000, drive_sites::kHalo2ControlSlotRva + 0x1000);
}

drive_known::Outcome ByRungs(const Plan& plan, const Image& image, uint32_t faults, bool recordedBuild,
                             const drive_known::Entry* saved, Result& result, Report& report)
{
    Result (*run)(Resolver&) = plan.run;
    return drive_known::RunTyped(image, faults, recordedBuild, saved, run, result, report);
}

// A line whose address holds a row's own bytes (a function start is one: the function must begin with them).
bool IsRow(const Line& line)
{
    return line.how == How::Signature || line.how == How::FunctionOffset || line.how == How::FunctionStart ||
           line.how == How::Known;
}

void TestPlan(const Plan& plan)
{
    g_plan = plan.name;
    // AS RECORDED.
    Synth s(plan.code, ImageSize(plan), 0);
    plan.build(s);
    const Image& image = s.Finish(plan.tlsIndex);
    Resolver search(image, 0);
    const Result searched = plan.run(search);
    const Report healthy = search.report();
    Check(searched.ok && searched.hook != 0, "the plan resolves on the image its tables describe");
    Check(healthy.Complete() && healthy.Count(Verdict::AsRecorded) == healthy.count,
          "the search finds every line, each where it is recorded");
    // A plan that outgrew a report would be refused whole (drive_known::kTooManyLines): none may come near it.
    Check(healthy.dropped == 0 && healthy.count * 4 <= kMaxLines * 3,
          "the plan fits a report with a quarter of it to spare");
    if (!healthy.Complete())
    {
        for (uint32_t i = 0; i < healthy.count; ++i)
            if (!Found(healthy.lines[i].verdict))
                std::printf("      %s: %s%s%s\n", healthy.lines[i].name, VerdictText(healthy.lines[i].verdict),
                            healthy.lines[i].note ? ": " : "", healthy.lines[i].note ? healthy.lines[i].note : "");
        return;
    }
    Result fast;
    Report known;
    drive_known::Outcome out = ByRungs(plan, image, 0, true, nullptr, fast, known);
    Check(out.rung == drive_known::Rung::Recorded && out.scans == 0 && fast.ok && fast.hook == searched.hook &&
              drive_known::SameAddresses(known, healthy) && known.plan == healthy.plan,
          "the known addresses verify with no scan and choose what the search chose");

    // MOVED: all code 0x40 later.
    {
        Synth moved(plan.code, ImageSize(plan), 0x40);
        plan.build(moved);
        const Image& there = moved.Finish(plan.tlsIndex);
        Result first;
        Report report;
        out = ByRungs(plan, there, 0, true, nullptr, first, report);
        bool followed = report.count == healthy.count;
        for (uint32_t i = 0; followed && i < report.count; ++i)
        {
            const Line& was = healthy.lines[i];
            const Line& now = report.lines[i];
            const bool code = now.how == How::Signature || now.how == How::FunctionOffset || now.how == How::FunctionStart;
            followed = code ? now.found == was.found + 0x40 && now.verdict == Verdict::Moved
                            : now.found == was.found || now.found == was.found + 0x40;
        }
        Check(out.rung == drive_known::Rung::Search && out.recordedTried && first.ok && first.hook == searched.hook + 0x40 &&
                  report.Complete() && followed && out.learn,
              "moved 0x40: the recorded addresses fail, the search follows every line, and the result is worth saving");
        drive_known::Entry entry;
        Result second;
        Report again;
        const bool made = drive_known::MakeEntry(entry, "test.dll", "plan", 0x1234, report);
        out = ByRungs(plan, there, 0, true, made ? &entry : nullptr, second, again);
        Check(made && out.rung == drive_known::Rung::Saved && out.scans == 0 && second.ok && second.hook == first.hook &&
                  drive_known::SameAddresses(again, report),
              "moved 0x40: the next run verifies the saved addresses with no scan");
    }

    // ONE ROW BROKEN AT A TIME: its first byte is not the game's any more.
    unsigned rows = 0, wandered = 0, stillFound = 0, rungOneStood = 0, hookedWithoutEntry = 0, optional = 0;
    for (uint32_t i = 0; i < healthy.count; ++i)
    {
        if (!IsRow(healthy.lines[i]))
            continue;
        ++rows;
        uint8_t& byte = s.bytes[healthy.lines[i].found];
        byte ^= 0xFF;
        Result broken;
        Report report;
        out = ByRungs(plan, image, 0, true, nullptr, broken, report);
        byte ^= 0xFF;
        rungOneStood += out.rung == drive_known::Rung::Recorded ? 1u : 0u;
        bool lineFound = false;
        for (uint32_t k = 0; k < report.count; ++k)
        {
            const Line& now = report.lines[k];
            if (!now.found || now.how == How::Proof)
                continue;
            // Whatever is still found is where the healthy plan found a line of that name.
            bool known = false;
            for (uint32_t h = 0; h < healthy.count && !known; ++h)
                known = healthy.lines[h].found == now.found && !std::strcmp(healthy.lines[h].name, now.name);
            wandered += known ? 0u : 1u;
            lineFound = lineFound || (now.found == healthy.lines[i].found && !std::strcmp(now.name, healthy.lines[i].name));
        }
        stillFound += lineFound ? 1u : 0u;
        if (healthy.lines[i].found == searched.hook && broken.ok)
            ++hookedWithoutEntry;
        optional += broken.ok ? 1u : 0u;
    }
    Check(rows >= 3, "the plan has rows to break");
    Check(stillFound == 0, "a broken row is never reported found");
    Check(wandered == 0, "breaking one row makes no other line wander to another address");
    Check(rungOneStood == 0, "with any one row broken the known addresses fail as a whole, and the search decides");
    Check(hookedWithoutEntry == 0, "a broken hooked function means no hook");
    std::printf("  %-36s %2u lines, %2u rows broken one at a time; %u of them leave the plan resolved (optional rows)\n",
                plan.name, healthy.count, rows, optional);
}

// ---- a control update's two plans ------------------------------------------------------------------------------------
// The player build's plan is the dev build's without its two tick anchors: the same addresses for the lines they
// share, and another version of the drive's tables. The two builds keep their saved addresses under the same
// module and drive, so what one saved the other finds -- and must not take.
void TestPlayerAndDevPlans()
{
    const Plan* dev = nullptr;
    unsigned pairs = 0;
    for (const Plan& player : kPlans)
    {
        if (!player.player)
            continue;
        dev = nullptr;
        for (const Plan& other : kPlans)
            if (!other.player && other.build == player.build)
                dev = &other;
        if (!dev)
            continue;   // a drive whose plan is the same in both builds
        ++pairs;
        g_plan = dev->name;
        Synth moved(player.code, ImageSize(player), 0x40);
        player.build(moved);
        const Image& image = moved.Finish(player.tlsIndex);
        Resolver asPlayer(image, 0), asDev(image, 0);
        const Result shipped = player.run(asPlayer), withTick = dev->run(asDev);
        const Report& a = asPlayer.report();
        const Report& b = asDev.report();
        bool sameLines = a.count + 2 == b.count;
        for (uint32_t i = 0; sameLines && i < a.count; ++i)
            sameLines = a.lines[i].found == b.lines[i].found && !std::strcmp(a.lines[i].name, b.lines[i].name);
        Check(shipped.ok && withTick.ok && shipped.hook == withTick.hook && a.Complete() && b.Complete() && sameLines,
              "the player build's plan is the dev build's without its two tick anchors: the same hook, the same "
              "address for every line they share");
        Check(a.plan != b.plan, "and another version of the drive's tables: its plan hashes differently");
        drive_known::Entry byPlayer, byDev;
        const bool made = drive_known::MakeEntry(byPlayer, "test.dll", "control-update", 0x1234, a) &&
                          drive_known::MakeEntry(byDev, "test.dll", "control-update", 0x1234, b);
        Result result;
        Report report;
        drive_known::Outcome out = ByRungs(*dev, image, 0, false, &byPlayer, result, report);
        Check(made && out.rung == drive_known::Rung::Search && out.savedTried &&
                  out.savedRefused == drive_known::kSavedOtherPlan && result.ok && result.hook == shipped.hook,
              "what a player build saved is not taken by a dev build: another version of the tables, searched again");
        out = ByRungs(player, image, 0, false, &byDev, result, report);
        Check(made && out.rung == drive_known::Rung::Search && out.savedRefused == drive_known::kSavedOtherPlan &&
                  result.ok && result.hook == shipped.hook,
              "nor what a dev build saved by a player build, and the reason given is the same");
        out = ByRungs(player, image, 0, false, &byPlayer, result, report);
        Check(made && out.rung == drive_known::Rung::Saved && out.scans == 0 && result.ok && result.hook == shipped.hook,
              "each takes its own with no scan");
    }
    g_plan = "the two builds' plans";
    Check(pairs == 4, "four control updates have a plan of each build");
}

// ---- proofs that are not rows ----------------------------------------------------------------------------------------
const Line* LineNamed(const Report& report, const char* name)
{
    for (uint32_t i = 0; i < report.count; ++i)
        if (!std::strcmp(report.lines[i].name, name))
            return &report.lines[i];
    return nullptr;
}

void TestProofs()
{
    g_plan = "proofs";
    // A caller whose call goes somewhere else is not that caller; the map's other roles stand.
    {
        Synth s(0x70000, drive_sites::kReachTlsIndexRva + 0x1000, 0);
        BuildReachControl(s);
        const drive_sites::CallSite& stick = drive_sites::kReachCallerAnchors.sites[0];
        s.Call(stick.rva + stick.callOffset, drive_sites::kReachControlAnchors[0].rva);   // not the dispatcher
        Resolver r(s.Finish(drive_sites::kReachTlsIndexRva), 0);
        const drive_sites::ReachControl f = drive_sites::ResolveReachControl(r);
        const drive_sites::Callers c = drive_sites::ResolveCallers(r, drive_sites::kReachCallerAnchors, f.entry);
        const Line* line = LineNamed(r.report(), stick.callName);
        Check(f.ok && c.frame && !c.call[0] && c.call[1] && c.call[2] && line && line->verdict == Verdict::Refused,
              "a role whose call does not land on the dispatcher is refused; the hook and the other roles stand");
    }
    // A dispatcher whose unwind info gives another frame than its prologue: the frame is a line, and it fails.
    {
        Synth s(0x70000, drive_sites::kReachTlsIndexRva + 0x1000, 0);
        BuildReachControl(s);
        const Image& image = s.Finish(drive_sites::kReachTlsIndexRva);
        const RuntimeFunction* dispatcher = EntryAt(image, drive_sites::kReachCallerAnchors.dispatcher.rva);
        Check(dispatcher != nullptr, "the dispatcher has an entry");
        if (dispatcher)
            s.bytes[dispatcher->unwind + 6] ^= 0x08;   // the allocation, eight bytes off
        Resolver r(image, 0);
        const drive_sites::ReachControl f = drive_sites::ResolveReachControl(r);
        const drive_sites::Callers c = drive_sites::ResolveCallers(r, drive_sites::kReachCallerAnchors, f.entry);
        const Line* line = LineNamed(r.report(), "dispatcher's frame");
        Check(f.ok && !c.frame && !c.call[0] && !c.call[1] && line && line->verdict == Verdict::Refused && line->note &&
                  !r.report().Complete(),
              "a frame the unwind info and the prologue disagree on is a line that fails, and no caller is proven");
        // The same failure in a pilot's plan: no frame, no hook.
        Synth p(0x530000, drive_sites::kReachTlsIndexRva + 0x1000, 0);
        BuildReachSabre(p);
        const Image& sabre = p.Finish(drive_sites::kReachTlsIndexRva);
        const RuntimeFunction* entry = EntryAt(sabre, drive_sites::kReachCallerAnchors.dispatcher.rva);
        if (entry)
            p.bytes[entry->unwind + 6] ^= 0x08;
        Resolver q(sabre, 0);
        const drive_sites::ReachSabre found = drive_sites::ResolveReachSabre(q);
        Check(entry && found.flight.entry && !found.flight.frame && !found.flight.ok,
              "a pilot's drive without a proven dispatcher frame is not hooked");
    }
    // THE ROLES ARE DIFFERENT CALLS.
    {
        Synth s(0x4000, 0x10000, 0);
        s.FunctionAt(0x2000).end = 0x2100;   // one function holds 0x2010 and 0x2050
        s.FunctionAt(0x3000).end = 0x3100;
        Resolver r(s.Finish(0), 0);
        const uint32_t recorded[drive_sites::kMaxCallers] = {0x2010, 0x2050, 0x3010, 0};
        const uint32_t asRecorded[drive_sites::kMaxCallers] = {0x2010, 0x2050, 0x3010, 0};
        const uint32_t oneCall[drive_sites::kMaxCallers] = {0x2010, 0x2010, 0x3010, 0};
        const uint32_t swapped[drive_sites::kMaxCallers] = {0x2050, 0x2010, 0x3010, 0};
        const uint32_t apart[drive_sites::kMaxCallers] = {0x3010, 0x2050, 0, 0};   // other functions: any order
        const uint32_t alone[drive_sites::kMaxCallers] = {0, 0x2050, 0, 0};
        Check(drive_sites::RolesDistinct(r, asRecorded, recorded) && drive_sites::RolesDistinct(r, alone, recorded),
              "roles found as recorded are distinct; a role found alone has nothing to be confused with");
        Check(!drive_sites::RolesDistinct(r, oneCall, recorded), "two roles at one call are not two roles");
        Check(!drive_sites::RolesDistinct(r, swapped, recorded), "two roles of one function in the other order are refused");
        Check(drive_sites::RolesDistinct(r, apart, recorded), "roles in different functions are not ordered by address");
        uint32_t calls[drive_sites::kMaxCallers] = {0x2050, 0x2010, 0x3010, 0};
        Check(!drive_sites::ProveRoles(r, calls, recorded) && !calls[0] && !calls[1] && !calls[2] &&
                  LineNamed(r.report(), "caller roles") && LineNamed(r.report(), "caller roles")->verdict == Verdict::Refused,
              "roles that are not provably different calls prove nothing: the whole map is cleared, and a line says so");
    }
    // A SHORT ANCHOR IS TAKEN AT ITS OFFSET ONLY, and an offset must be inside the function.
    {
        constexpr char kShort[] = "4C 8D 7B 18";
        constexpr char kLong[] = "F3 0F 10 84 BE CC 00 00 00 B8 90 00 00 00 48 8B 0C 08";
        constexpr char kEntry[] = "48 8B C4 55 53 56 57 41 54 48 8D A8 18 FF FF FF 48 81 EC A8 01 00 00";
        Synth s(0x4000, 0x10000, 0);
        s.Cover(0x2000, 0x2000, s.Put(0x2000, kEntry));
        s.Cover(0x2000, 0x2070, s.Put(0x2070, kShort));   // recorded at +0x60; it stands at +0x70
        s.Cover(0x2000, 0x20B0, s.Put(0x20B0, kLong));    // recorded at +0xA0; it stands at +0xB0
        const Image& image = s.Finish(0);
        Resolver r(image, 0);
        const uint32_t entry = r.Unique("entry", 0x2000, kEntry);
        Check(r.InFunction("long", 0x20A0, 0x2000, entry, kLong) == 0x20B0 && r.report().lines[1].how == How::FunctionExtent,
              "a long anchor that moved inside its function is found as the one match there");
        Check(r.InFunction("short", 0x2060, 0x2000, entry, kShort, true) == 0 &&
                  r.report().lines[2].verdict == Verdict::Refused && r.report().lines[2].note == kOffsetOnly,
              "a short anchor that is not at its offset is refused, though it stands alone in the function");
        Check(r.InFunction("short at its offset", 0x2070, 0x2000, entry, kShort, true) == 0x2070,
              "and taken where it is at its offset");
        Check(drive_sites::ShortSignature(kShort) && drive_sites::ShortSignature("8B 8C 1F 20 01 00 00") &&
                  !drive_sites::ShortSignature(kLong),
              "which rows are short: fewer than twelve bytes");
        // An offset that lands beyond the function's end, on bytes that match, is not in the function.
        Synth t(0x4000, 0x10000, 0);
        t.Cover(0x2000, 0x2000, t.Put(0x2000, kEntry));
        t.Put(0x2400, kLong);   // outside the function, exactly at the recorded offset from its start
        Resolver q(t.Finish(0), 0);
        const uint32_t start = q.Unique("entry", 0x2000, kEntry);
        Check(q.InFunction("long", 0x2400, 0x2000, start, kLong) == 0 && !Found(q.report().lines[1].verdict),
              "bytes at the recorded offset but past the function's end are not taken");
        // A saved address for a short anchor must be the offset too.
        const uint32_t saved[3] = {0x2000, 0x2070, 0};
        Resolver k(image, 0, Known{Source::Saved, saved, 2});
        const uint32_t again = k.Unique("entry", 0x2000, kEntry);
        Check(k.InFunction("short", 0x2060, 0x2000, again, kShort, true) == 0,
              "a saved address for a short anchor is not taken off its offset either");
    }
    // A PAGE THAT CANNOT BE READ costs the line that needed it, not the plan.
    {
        static uint32_t forbidden = 0;
        static const uint8_t* base = nullptr;
        Synth s(0x70000, drive_sites::kReachTlsIndexRva + 0x1000, 0);
        BuildReachControl(s);
        Image image = s.Finish(drive_sites::kReachTlsIndexRva);
        base = image.base;
        image.readable = [](uintptr_t address, size_t size) {
            const uintptr_t page = reinterpret_cast<uintptr_t>(base) + (forbidden & ~0xFFFu);
            return !forbidden || address + size <= page || address >= page + 0x1000;
        };
        // The dispatcher's unwind info: its frame cannot be proven, the hook stands.
        const RuntimeFunction* dispatcher = EntryAt(image, drive_sites::kReachCallerAnchors.dispatcher.rva);
        forbidden = dispatcher ? dispatcher->unwind : 0;
        // (every function's unwind info shares that page here, so function starts are not provable either)
        Resolver r(image, 0, Known{Source::Recorded, nullptr, 0});
        const drive_sites::ReachControl f = drive_sites::ResolveReachControl(r);
        Check(dispatcher && !f.ok && !f.entry, "unwind info that cannot be read proves no function start: not found, no fault");
        // One caller's page: that caller is lost, the rest stands.
        forbidden = drive_sites::kReachCallerAnchors.sites[1].rva;   // the impulse call site, far from the rest
        Resolver q(image, 0, Known{Source::Recorded, nullptr, 0});
        const drive_sites::ReachControl g = drive_sites::ResolveReachControl(q);
        const drive_sites::Callers c = drive_sites::ResolveCallers(q, drive_sites::kReachCallerAnchors, g.entry);
        Check(g.ok && c.frame && c.call[0] && !c.call[1] && c.call[2],
              "a caller on a page that cannot be read is lost; the hook, the frame and the other callers stand");
        forbidden = 0;
    }
    // A function's extent is searched only where the module can be read.
    {
        Synth s(0x4000, 0x10000, 0);
        constexpr char kLong[] = "F3 0F 10 84 BE CC 00 00 00 B8 90 00 00 00 48 8B 0C 08";
        s.Put(0x2100, kLong);
        s.Put(0x2900, kLong);
        Image image = s.Finish(0);
        Pattern p;
        Compile(kLong, p);
        Check(CountInExecutable(image, p, 0x2000, 0x3000).count == 2, "two matches in a range that is all readable");
        image.executable[0] = Range{0x1000, 0x1800};   // readable runs: [0x1000, 0x2800) and [0x2A00, ...)
        image.executable[1] = Range{0x2A00, 0x2000};
        image.executableCount = 2;
        Check(CountInExecutable(image, p, 0x2000, 0x3000).count == 1 && CountInExecutable(image, p, 0x2000, 0x3000).first == 0x2100,
              "a match in a hole between the readable runs is not read, and not counted");
    }
    // A proof is a line that counts.
    {
        Synth s(0x4000, 0x10000, 0);
        Resolver r(s.Finish(0), 0);
        Check(r.Proven("a proof", 0x100, 0x100, nullptr) == 0x100 && r.report().Complete() &&
                  r.report().lines[0].how == How::Proof && r.report().lines[0].verdict == Verdict::AsRecorded,
              "a proof that holds is a found line");
        Check(r.Proven("another", 0x100, 0, "why not") == 0 && !r.report().Complete() &&
                  r.report().lines[1].verdict == Verdict::Refused && !std::strcmp(r.report().lines[1].note, "why not"),
              "a proof that fails is a line that is not found, with its reason: the drive is not whole");
    }
}
} // namespace

int main()
{
    std::printf("the real plans of drive_sites.h, on images synthesised from its tables:\n");
    for (const Plan& plan : kPlans)
        TestPlan(plan);
    TestPlayerAndDevPlans();
    TestProofs();
    std::printf("drive sites tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
