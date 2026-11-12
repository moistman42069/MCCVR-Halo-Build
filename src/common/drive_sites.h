#pragma once
// DRIVE SITES -- what each direct drive needs to find in its game DLL, and how it is found.
//
// One table per drive: the function it hooks, the instructions that prove the fields it reads, the call sites
// that name its callers. Every RVA here is a measurement of the retail Steam build of MCC 2025.08.16.178512.1
// (the Store modules are byte-identical: docs/MCC-EDITIONS-EVIDENCE.md), and none is trusted to say where anything
// is. Each drive's Resolve function resolves its sites in the image it is given -- verified at the addresses they
// are already known to stand at, or found by search (drive_resolve.h: by signature anywhere in the executable
// sections, by function for the short field anchors, by derivation for the rest; drive_known.h: which of the
// two, and in what order) -- and makes the address-level proofs on those addresses either way: a call's rel32
// lands on the entry, an anchor lies in the function that is hooked, the dispatcher's frame agrees between its
// unwind info and its prologue, the engine TLS index is the module's own. A Resolve function does not know
// which rung it is run on: it asks the same things in the same order, which is what lets a saved resolution be
// verified line for line. The recorded RVA is the cross-check that says "as recorded" or "moved".
//
// The DLL, the unit tests and the offline check against the real game DLLs (tools/drive_address_check.cpp) all
// include this one file: there is no second copy of any pattern.
//
// ADDR-HYGIENE: resolved. Every `rva`, `...Rva` and offset-into-a-pattern below is the recorded half of a
// resolution: a drive hooks or reads only what its Resolve function found and proved, and a site that is not
// found fails closed (no hook, one line naming it; the stick servo keeps aiming). Struct offsets the drives read
// (a record's angles, a vehicle's facing) are not here: they stand in the patterns' own displacement bytes, which
// are exact, so a layout change stops the pattern matching and the drive is not hooked.
//
// Zero engine dependencies.

#include <cstddef>
#include <cstdint>

#include "aim_caller_logic.h"
#include "drive_resolve.h"
#include "runtime_types.h"

namespace drive_sites
{
using drive_resolve::Resolver;

// ---------------------------------------------------------------------------------------------------- rows
// How a row of a table is found.
enum class Find : uint8_t
{
    Anywhere,   // by its signature, anywhere in the executable sections
    Entry,      // the same, and it must be where a function starts (.pdata): a function that is hooked
    Member,     // the same, and it must lie in the function that holds row `via`
    Within,     // in the function that holds row `via`: at its recorded offset from that function's start, else
                // the one match inside it. For the short anchors: the instruction that reads a field.
    StartOf,    // the start of the function that holds row `via` (its own first bytes are shared by others)
};

struct Where
{
    Find find;
    int8_t via;             // an EARLIER row of the same table, or -1
    uint32_t functionRva;   // Within: where that function starts in the recorded build
};

inline constexpr Where kAnywhere{Find::Anywhere, -1, 0};
inline constexpr Where kEntry{Find::Entry, -1, 0};
constexpr Where MemberOf(int viaRow) { return Where{Find::Member, static_cast<int8_t>(viaRow), 0}; }
constexpr Where Within(int viaRow, uint32_t functionRva)
{
    return Where{Find::Within, static_cast<int8_t>(viaRow), functionRva};
}
constexpr Where StartOf(int viaRow) { return Where{Find::StartOf, static_cast<int8_t>(viaRow), 0}; }

struct Anchor
{
    const char* name;
    uint32_t rva;
    Where where;
    const char* pattern;
};

// A SHORT SIGNATURE IS TAKEN AT ITS RECORDED OFFSET ONLY. A row resolved inside its function is normally looked
// for at its recorded offset from the function's start and, failing that, accepted as the one match inside the
// function. For the one or two instructions that read a field -- fewer than kMinSearchedBytes bytes, down to the
// four of `lea r15, [rbx+0x14]` -- a lone match somewhere in a changed function is no proof of which instruction
// it is, and what the row proves is a struct offset. Those rows fail closed when they are not at their offset.
inline constexpr uint32_t kMinSearchedBytes = 12;
inline bool ShortSignature(const char* pattern) { return drive_resolve::PatternLength(pattern) < kMinSearchedBytes; }

inline constexpr char kNotAFunctionStart[] = "it is not where a function starts (.pdata)";
inline constexpr char kNotInItsFunction[] = "it is not inside the function it belongs to";
inline constexpr char kItsFunctionNotFound[] = "the function it belongs to was not found";

// Resolves one row given what its `via` row was found at.
inline uint32_t ResolveRow(Resolver& r, const Anchor& a, uint32_t via)
{
    uint32_t found = 0;
    switch (a.where.find)
    {
    case Find::Anywhere:
        return r.Unique(a.name, a.rva, a.pattern);
    case Find::Entry:
        found = r.Unique(a.name, a.rva, a.pattern);
        return found && !r.IsFunctionStart(found) ? r.Refuse(kNotAFunctionStart) : found;
    case Find::Member:
        found = r.Unique(a.name, a.rva, a.pattern);
        if (found && !via)
            return r.Refuse(kItsFunctionNotFound);
        return found && !r.SameFunction(found, drive_resolve::FunctionStartOf(r.image(), via))
            ? r.Refuse(kNotInItsFunction) : found;
    case Find::Within:
        return r.InFunction(a.name, a.rva, a.where.functionRva, via, a.pattern, ShortSignature(a.pattern));
    case Find::StartOf:
        return r.FunctionStart(a.name, a.rva, via, a.pattern);
    }
    return 0;
}

// Resolves a table's rows in order (a row's `via` is an earlier row). found[i]: the RVA, or 0. Returns how many
// rows were not found.
template <size_t N>
inline uint32_t ResolveRows(Resolver& r, const Anchor (&rows)[N], uint32_t (&found)[N])
{
    uint32_t missing = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const int via = rows[i].where.via;
        found[i] = ResolveRow(r, rows[i], via >= 0 && static_cast<size_t>(via) < i ? found[via] : 0);
        missing += found[i] ? 0u : 1u;
    }
    return missing;
}

// A table is well formed when every `via` names an earlier row that is found by its own signature, and a Within
// row and its `via` row both lie at or after the function start the row records.
template <size_t N>
constexpr bool RowsWellFormed(const Anchor (&rows)[N])
{
    for (size_t i = 0; i < N; ++i)
    {
        const Where& w = rows[i].where;
        if (w.find == Find::Anywhere || w.find == Find::Entry)
            continue;
        if (w.via < 0 || static_cast<size_t>(w.via) >= i)
            return false;
        const Find viaFind = rows[w.via].where.find;
        if (viaFind != Find::Anywhere && viaFind != Find::Entry && viaFind != Find::Member)
            return false;
        if (w.find == Find::Within && (rows[i].rva < w.functionRva || rows[w.via].rva < w.functionRva))
            return false;
    }
    return true;
}

constexpr bool SameText(const char* a, const char* b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}

// The index of the row named `name`, or -1.
template <size_t N>
constexpr int RowIndex(const Anchor (&rows)[N], const char* name)
{
    for (size_t i = 0; i < N; ++i)
        if (SameText(rows[i].name, name))
            return static_cast<int>(i);
    return -1;
}

// ------------------------------------------------------------------------------------------------- callers
// One caller of the control update (or of the dispatcher every caller goes through): the bytes that lead up to
// its call, and the `call` (E8 rel32) inside them. Each must be unique in the module; only rel32 call targets and
// rip-relative displacements are wildcards.
struct CallSite
{
    const char* name;       // its bytes, as the report names them (null: no such site)
    const char* callName;   // ... and its call instruction
    uint32_t rva;
    const char* pattern;
    uint8_t callOffset;
    aim_caller::Caller role;
};

// A caller's call instruction: its bytes found by signature, and the E8 inside them proven to land on `target`
// (the found control update, or the found dispatcher). 0: not proven, and its calls read as unknown.
inline uint32_t ResolveCallSite(Resolver& r, const CallSite& site, uint32_t target)
{
    const uint32_t at = r.Unique(site.name, site.rva, site.pattern);
    const uint32_t call = r.Derived(site.callName, site.rva + site.callOffset, at ? at + site.callOffset : 0);
    if (call && !(target && r.CallTarget(call) == target))
        return r.Refuse("its call does not land on the function it is recorded to call");
    return call;
}

inline constexpr int kMaxCallers = 4;

// WHICH CALLER RAN THE CONTROL UPDATE (aim_caller_logic.h). Halo 3 and ODST call it directly from each site
// (direct mode: accumulatorCall has no pattern). Reach and Halo 4 call it from ONE site inside a per-user
// dispatcher that every caller goes through (dispatcher mode): the control update's one call site, the
// dispatcher's prologue, and the dispatcher's own callers.
struct CallerAnchors
{
    CallSite accumulatorCall;
    Anchor dispatcher;
    CallSite sites[kMaxCallers];
    aim_caller::Action noUnitAction;
    uint32_t frame = 0;   // dispatcher mode: the frame the recorded build's dispatcher proves (the cross-check)
};

struct Callers
{
    uint32_t accumulatorCall = 0;     // dispatcher mode: the control update's one call
    uint32_t dispatcher = 0;          // dispatcher mode: where the dispatcher starts
    uint32_t frame = 0;               // dispatcher mode: its return address sits at control-update entry rsp + this
    uint32_t call[kMaxCallers]{};     // each site's call instruction, in the table's order (0: not proven)
    const char* why = nullptr;        // dispatcher mode: the first proof that failed
};

// The frame through which a dispatcher's own return address is read from the entry rsp of the function it calls
// at `call`: aim_caller::ProveDispatcherFrame over the dispatcher's own .pdata entry (its unwind info and its
// prologue must decode to one fixed frame, and the entry must hold the call past its prolog). 0 with `why`.
inline uint32_t DispatcherFrame(const Resolver& r, uint32_t dispatcher, uint32_t call, const char*& why)
{
    const drive_resolve::Image& im = r.image();
    const drive_resolve::RuntimeFunction* entry = drive_resolve::EntryAt(im, dispatcher);
    if (!entry)
    {
        why = "dispatcher .pdata entry";
        return 0;
    }
    // What the proof reads -- the dispatcher's first bytes and its unwind info -- must be readable.
    const uint32_t prologue = im.size - dispatcher < 64 ? im.size - dispatcher : 64;
    if (dispatcher >= im.size || !drive_resolve::Readable(im, dispatcher, prologue) ||
        !drive_resolve::Readable(im, entry->unwind, 4) ||
        !drive_resolve::Readable(im, entry->unwind, 4u + 2u * im.base[entry->unwind + 2]))
    {
        why = "dispatcher bytes or unwind info cannot be read";
        return 0;
    }
    uint32_t frame = 0;
    return aim_caller::ProveDispatcherFrame(im.base, im.size, dispatcher, entry->begin, entry->end, entry->unwind,
                                            call, frame, why) ? frame : 0;
}

// THE ROLES ARE DIFFERENT CALLS. Two roles' signatures can differ in two or three bytes, and in a rebuilt game
// one could match where the other's call is; the wrong call would then be taken for the update. So no two found
// calls may be one instruction, and two that lie in one function must stand in the order the tables record them
// in. (They cannot be required to lie in different functions: in Reach and Halo 4 the stick update and the
// no-unit path are calls of ONE function.) `recorded[i]`: where the tables have call i. A role whose signature
// lands on another role's site while that other role is not found at all is not caught by this.
inline bool RolesDistinct(const Resolver& r, const uint32_t (&calls)[kMaxCallers], const uint32_t (&recorded)[kMaxCallers])
{
    for (int i = 0; i < kMaxCallers; ++i)
        for (int j = i + 1; j < kMaxCallers; ++j)
        {
            if (!calls[i] || !calls[j])
                continue;
            if (calls[i] == calls[j])
                return false;
            const uint32_t fi = drive_resolve::FunctionStartOf(r.image(), calls[i]);
            if (fi && fi == drive_resolve::FunctionStartOf(r.image(), calls[j]) &&
                (calls[i] < calls[j]) != (recorded[i] < recorded[j]))
                return false;
        }
    return true;
}

inline constexpr char kRolesNotDistinct[] =
    "two roles were found at one call, or in another order than recorded inside one function";

// The roles' proof as a line of its own. False: nothing of the map is proven, and `calls` is cleared.
inline bool ProveRoles(Resolver& r, uint32_t (&calls)[kMaxCallers], const uint32_t (&recorded)[kMaxCallers])
{
    bool any = false;
    for (const uint32_t call : calls)
        any = any || call != 0;
    if (!any)
        return true;
    if (r.Proven("caller roles", 1, RolesDistinct(r, calls, recorded) ? 1u : 0u, kRolesNotDistinct))
        return true;
    for (uint32_t& call : calls)
        call = 0;
    return false;
}

// `entry`: the found control update. In dispatcher mode nothing is resolved past the first proof that fails
// (the caller map is then not proven at all, and every call counts as the update).
inline Callers ResolveCallers(Resolver& r, const CallerAnchors& anchors, uint32_t entry)
{
    Callers f;
    uint32_t target = entry;
    if (anchors.accumulatorCall.pattern)
    {
        f.accumulatorCall = ResolveCallSite(r, anchors.accumulatorCall, entry);
        if (!f.accumulatorCall)
        {
            f.why = "the accumulator's call site";
            return f;
        }
        f.dispatcher = r.Unique(anchors.dispatcher.name, anchors.dispatcher.rva, anchors.dispatcher.pattern);
        if (f.dispatcher && !(r.IsFunctionStart(f.dispatcher) && r.SameFunction(f.accumulatorCall, f.dispatcher)))
            f.dispatcher = r.Refuse("it is not the start of the function that calls the control update");
        if (!f.dispatcher)
        {
            f.why = "the dispatcher prologue";
            return f;
        }
        // The frame is a line of the report: a map that needs it is not whole without it.
        const uint32_t frame = DispatcherFrame(r, f.dispatcher, f.accumulatorCall, f.why);
        f.frame = r.Proven("dispatcher's frame", anchors.frame, frame, f.why);
        if (!f.frame)
            return f;
        target = f.dispatcher;
    }
    uint32_t recorded[kMaxCallers]{};
    for (int i = 0; i < kMaxCallers; ++i)
        if (anchors.sites[i].pattern)
        {
            f.call[i] = ResolveCallSite(r, anchors.sites[i], target);
            recorded[i] = anchors.sites[i].rva + anchors.sites[i].callOffset;
        }
    if (!ProveRoles(r, f.call, recorded))
        f.why = kRolesNotDistinct;
    return f;
}

// The site of a role, or null.
constexpr const CallSite* SiteOf(const CallerAnchors& anchors, aim_caller::Caller role)
{
    for (int i = 0; i < kMaxCallers; ++i)
        if (anchors.sites[i].pattern && anchors.sites[i].role == role)
            return &anchors.sites[i];
    return nullptr;
}

// DEV CENSUS: a title's sim tick counter, [[TLS + slot]] + field. The title's own getter (its TLS-index load must
// name the drive's index; the slot imm32 sits at +0x10 and the field disp8 at +0x1E of it) and the increment
// inside the per-tick function (the same slot where its bytes carry one, at incSlotOff, and the same field at
// incFieldOff). Only dev builds resolve these; nothing reads the tick to decide anything.
struct TickAnchors
{
    uint32_t getterRva;
    const char* getter;
    uint32_t incRva;
    const char* inc;
    uint8_t incSlotOff;    // 0xFF: the slot is set earlier, not in these bytes
    uint8_t incFieldOff;
};

struct Tick
{
    uint32_t getter = 0;
    uint32_t increment = 0;
};

inline Tick ResolveTick(Resolver& r, const TickAnchors& a)
{
    Tick f;
    f.getter = r.Unique("sim tick getter", a.getterRva, a.getter);
    f.increment = r.Unique("sim tick increment", a.incRva, a.inc);
    return f;
}

// ======================================================================================================
// REACH: the per-user control update haloreach+0x6375C -- the accumulator of player_control.desired_angles --
// on foot and in every seat but a type-13 pilot's.
// ======================================================================================================
// ADDR-HYGIENE: resolved (ResolveReachControl, ResolveCallers, ResolveTick). Every RVA in this section is the
// recorded half of a resolution; the install hooks and reads only what was found in the loaded module.
// Where the module's TLS index sits in the recorded build. The live address is the PE TLS directory's.
inline constexpr uint32_t kReachTlsIndexRva = 0x00C17B18;

// The entry prologue (so the hook relocates known instructions; the frame size and the first xmm saves make it
// unique), the TLS/stride block (mov ecx,[rip+index]; mov edi,0xD8 ... lea rbx,[rsi+rsi*2] ... mov r14,[rax+rcx*8])
// and the accumulator itself (addss/movss [rdi+rbx+0x94]).
inline constexpr Anchor kReachControlAnchors[] = {
    {"control update", 0x6375C, kEntry,
     "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D 6C 24 80 "
     "48 81 EC 80 01 00 00 0F 29 70 C8 0F 29 78 B8"},
    {"TLS and stride block", 0x637FA, MemberOf(0),
     "8B 0D ?? ?? ?? ?? BF D8 00 00 00 F3 0F 11 5C 24 50 48 8D 1C 76 4C 8B 34 C8"},
    {"accumulator", 0x63AF9, MemberOf(0),
     "0F 28 D0 F3 0F 58 94 1F 94 00 00 00 41 83 CB FF B8 10 00 00 00 F3 0F 11 94 1F 94 00 00 00"},
};
static_assert(RowsWellFormed(kReachControlAnchors));

struct ReachControl
{
    uint32_t entry = 0, tlsBlock = 0, accumulator = 0;
    uint32_t tlsIndex = 0;   // the module's TLS index, which the TLS block's own instruction loads
    bool ok = false;
};

inline ReachControl ResolveReachControl(Resolver& r)
{
    ReachControl f;
    uint32_t found[3]{};
    (void)ResolveRows(r, kReachControlAnchors, found);
    f.entry = found[0];
    f.tlsBlock = found[1];
    f.accumulator = found[2];
    // `mov ecx, [rip + disp32]` opens the TLS block.
    f.tlsIndex = r.TlsIndex("engine TLS index", kReachTlsIndexRva, f.tlsBlock ? r.RipTarget(f.tlsBlock, 2, 6) : 0);
    f.ok = f.entry && f.tlsBlock && f.accumulator && f.tlsIndex;
    return f;
}

// 0x6375C has ONE call site, 0x6365E, inside the per-user dispatcher 0x6350C (a fixed 0xF8-byte frame: 7 pushes +
// 0xC0, no frame register), and one control pass (0x61A38, once per rendered frame) calls that dispatcher up to
// three times: [an effect camera impulse 0x5EE43] -> the stick update 0x62467, or [impulse] -> the no-unit path
// 0x628A3.
inline constexpr CallerAnchors kReachCallerAnchors = {
    {"control update's call site", "control update's call", 0x63651,
     "F3 0F 10 5D 77 0F 28 D7 0F 28 CE 8B CF E8 ?? ?? ?? ?? F2 0F 10 43 14", 0x0D, aim_caller::Caller::Unknown},
    {"dispatcher", 0x6350C, kEntry,
     "48 8B C4 55 53 57 41 54 41 55 41 56 41 57 48 8D 68 B1 48 81 EC C0 00 00 00 0F 29 70 B8 0F 29 78 A8"},
    {{"stick update", "stick update's call", 0x6244A,
      "F3 0F 10 55 88 4C 8D 4C 24 70 F3 0F 10 4D 84 8B CF F3 0F 11 7C 24 28 F3 0F 11 74 24 20 "
      "E8 ?? ?? ?? ??", 0x1D, aim_caller::Caller::Stick},
     {"camera impulse", "camera impulse's call", 0x5EE1C,
      "4C 8D 0D ?? ?? ?? ?? F3 0F 10 55 34 41 0F 57 CE F3 44 0F 10 74 24 50 8B CB F3 44 0F 11 74 24 28 "
      "F3 44 0F 11 6C 24 20 E8 ?? ?? ?? ??", 0x27, aim_caller::Caller::Impulse},
     {"no-unit path", "no-unit path's call", 0x62885,
      "F3 0F 10 55 88 4C 8D 4C 24 70 F3 0F 10 4D 84 41 8B CD F3 0F 11 7C 24 28 F3 0F 11 74 24 20 "
      "E8 ?? ?? ?? ??", 0x1E, aim_caller::Caller::NoUnit},
     {}},
    aim_caller::Action::EndUpdate, 0x100};

// Dev census: the tick getter 0x5C410 and the increment in the tick function 0x575AC ([[TLS+0xA0]]+0xC).
inline constexpr TickAnchors kReachTickAnchors{
    0x5C410, "8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 BA A0 00 00 00 48 8B 04 C8 48 8B 0C 10 8B 41 0C C3",
    0x57A4C, "F6 44 24 7C 01 4C 89 3D ?? ?? ?? ?? 74 0C B8 A0 00 00 00 48 8B 04 18 FF 40 0C", 15, 25};

// ======================================================================================================
// THE TYPE-13 PILOT CONTROLLERS: Reach's Sabre / Seraph (haloreach+0x64828) and Halo 4's Broadsword
// (halo4+0xAA524). The title's dispatcher calls the controller INSTEAD of the control update for such a pilot.
// ======================================================================================================
// ADDR-HYGIENE: resolved (ResolveFlight; ResolveReachSabre here, ResolveHalo4Flight after Halo 4's control
// update). Every RVA is the recorded half of a resolution, and a row that reads a field is found inside the
// function it belongs to.
// What a pilot controller's drive resolves beyond its rows.
struct FlightSites
{
    int entryRow;             // the controller: the function that is hooked
    int callRow;              // the dispatcher's type test and `call controller`
    uint32_t callOffset;      // ... the call (E8 rel32) inside that row
    int tlsRow;               // the row holding the controller's load of the engine TLS index
    uint32_t tlsLoadOffset;   // ... the instruction inside it
    uint32_t tlsDisplacement; // ... its disp32
    uint32_t tlsLength;       // ... and its length
    uint32_t tlsIndexRva;     // where the module's TLS index sits in the recorded build
    const CallerAnchors* callers;        // the dispatcher (its prologue) and its callers
    aim_caller::Caller order[kMaxCallers];   // those callers in the drive's own order (Unknown: none)
};

// Every row a FlightSites names exists in its table (a misspelt row name is a compile error, not a read of row -1).
constexpr bool Valid(const FlightSites& s)
{
    return s.entryRow >= 0 && s.callRow >= 0 && s.tlsRow >= 0 && s.callers && s.callers->dispatcher.pattern;
}

struct Flight
{
    uint32_t entry = 0;
    uint32_t call = 0;                    // the dispatcher's call of the controller
    uint32_t dispatcher = 0;              // the function that call sits in (.pdata)
    uint32_t frame = 0;                   // the dispatcher's return address: controller entry rsp + this
    const char* frameWhy = nullptr;
    uint32_t tlsIndex = 0;
    uint32_t callerCall[kMaxCallers]{};   // each of the dispatcher's callers, in the drive's order (0: not proven)
    uint32_t stickCall = 0;               // ... the stick update's among them: the update the drive writes after
    uint32_t missing = 0;                 // rows not found
    // Every row, the controller's call, the dispatcher and its frame, the TLS index and the stick update's call:
    // the hook may exist.
    bool ok = false;
};

template <size_t N>
inline Flight ResolveFlight(Resolver& r, const Anchor (&rows)[N], uint32_t (&found)[N], const FlightSites& s)
{
    Flight f;
    f.missing = ResolveRows(r, rows, found);
    f.entry = found[s.entryRow];
    // The dispatcher's call of the controller: at a fixed place inside the call row, and it must land on the entry.
    f.call = r.Derived("controller's call", rows[s.callRow].rva + s.callOffset,
                       found[s.callRow] ? found[s.callRow] + s.callOffset : 0);
    if (f.call && !(f.entry && r.CallTarget(f.call) == f.entry))
        f.call = r.Refuse("it does not call the pilot's controller");
    // The dispatcher is the function that call sits in, and begins with the dispatcher's own prologue.
    f.dispatcher = r.FunctionStart(s.callers->dispatcher.name, s.callers->dispatcher.rva, f.call,
                                   s.callers->dispatcher.pattern);
    if (f.dispatcher)
    {
        const uint32_t frame = DispatcherFrame(r, f.dispatcher, f.call, f.frameWhy);
        f.frame = r.Proven("dispatcher's frame", s.callers->frame, frame, f.frameWhy);
    }
    f.tlsIndex = r.TlsIndex("engine TLS index", s.tlsIndexRva,
                            found[s.tlsRow]
                                ? r.RipTarget(found[s.tlsRow] + s.tlsLoadOffset, s.tlsDisplacement, s.tlsLength)
                                : 0);
    uint32_t recorded[kMaxCallers]{};
    int stick = -1;
    for (int i = 0; i < kMaxCallers; ++i)
    {
        const CallSite* site = s.order[i] == aim_caller::Caller::Unknown ? nullptr : SiteOf(*s.callers, s.order[i]);
        if (site && f.dispatcher)
        {
            f.callerCall[i] = ResolveCallSite(r, *site, f.dispatcher);
            recorded[i] = site->rva + site->callOffset;
        }
        if (s.order[i] == aim_caller::Caller::Stick)
            stick = i;
    }
    // Roles that are not provably different calls prove nothing: no stick update, so no hook.
    f.stickCall = ProveRoles(r, f.callerCall, recorded) && stick >= 0 ? f.callerCall[stick] : 0;
    f.ok = !f.missing && f.entry && f.call && f.dispatcher && f.frame && f.tlsIndex && f.stickCall;
    return f;
}

// ---- Reach: the Sabre's pilot. Every row but ENTRY, CALL and HX is the instruction that reads a field the drive
// reads; those are resolved inside their function.
// The pilot's controller: the function row 0 (ENTRY) is in.
inline constexpr Where kReachSabreInController = Within(0, 0x64828);
// The hull: the function row 6 (HX) is in.
inline constexpr Where kReachSabreInHull = Within(6, 0x51DE3C);
inline constexpr Anchor kReachSabreAnchors[] = {
    {"ENTRY", 0x64828, kEntry,
     "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 18 FF FF FF 48 81 EC A8 01 00 00 0F 29 70 A8 0F 29 78 98 "
     "44 0F 29 40 88 44 0F 29 88 78 FF FF FF 44 0F 29 90 68 FF FF FF 44 0F 29 98 58 FF FF FF 44 0F 29 A0 48 FF FF FF "
     "44 0F 29 A8 38 FF FF FF 44 0F 29 B0 28 FF FF FF 44 0F 29 B8 18 FF FF FF 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 F0 "
     "65 48 8B 04 25 58 00 00 00 48 8D 35 ?? ?? ?? ?? 44 8B 05 ?? ?? ?? ?? 45 0F 57 C9 41 B9 10 00 00 00 41 BD D8 00 00 00"},
    // The dispatcher: physics type == 13 -> the controller -> jmp past the accumulator's call.
    {"CALL", 0x635D5, kAnywhere,
     "8B 08 E8 ?? ?? ?? ?? 83 F8 0D 75 ?? F3 0F 10 45 7F 0F 28 DF F3 0F 10 4D 77 0F 28 D6 8B 55 C7 8B CF "
     "F3 0F 11 44 24 30 F3 0F 11 4C 24 28 4C 89 6C 24 20 E8 ?? ?? ?? ?? EB ??"},
    // The record: player control = *(TLS + 0xD8) + user * 0x180; the controller works on +0x80 of it.
    {"REC", 0x648B3, kReachSabreInController,
     "41 B9 10 00 00 00 41 BD D8 00 00 00 F3 0F 11 54 24 48 4E 8B 04 C0 48 63 C1 0F B7 CA F3 0F 11 5C 24 50 4F 8B 2C 28 "
     "4C 8D 24 40 4C 89 45 A8 4B 8B 04 01 48 8D 14 49 49 C1 E4 07 48 89 54 24 68 49 8D 9D 80 00 00 00 49 03 DC"},
    // the Euler output: player control + 0x94 (yaw, pitch, roll)
    {"ANGPTR", 0x64C88, kReachSabreInController, "4C 8D 7B 14"},
    // The forced steer's stamp: direction vehicle +0xAC0 / +0xAC8, tick vehicle +0xABC.
    {"STAMP", 0x64D6A, kReachSabreInController,
     "F2 41 0F 10 85 C0 0A 00 00 41 8B 85 C8 0A 00 00 45 8B B5 BC 0A 00 00"},
    // Its window: the tag block's +0xAC against game time (TLS + 0xA0: the tick at +0xC, seconds per tick at +8).
    {"WIN", 0x64E58, kReachSabreInController,
     "F3 0F 10 84 BE AC 00 00 00 B9 A0 00 00 00 4A 8B 0C 21 8B 41 0C 41 2B C6 66 0F 6E C8 0F 5B C9 F3 0F 59 49 08 "
     "0F 2F C1 0F 86 ?? ?? ?? ??"},
    // The hull (0x51DE3C) reading the desired facing it steers by, vehicle +0x1E4 / +0x1E8 / +0x1EC ...
    {"HX", 0x51E551, kAnywhere,
     "F3 0F 10 B6 E8 01 00 00 F3 0F 10 BE E4 01 00 00 0F 28 C6 0F 28 CF E8 ?? ?? ?? ?? F3 0F 59 FF F3 0F 11 45 C0 "
     "F3 0F 59 F6 F3 0F 58 FE 0F 28 C7 E8 ?? ?? ?? ?? 0F 28 C8 F3 0F 10 86 EC 01 00 00 E8 ?? ?? ?? ?? F3 0F 10 B6 50 02 00 00"},
    // ... and the craft's own forward, vehicle +0x50 / +0x54 / +0x58 (the nose, for the report).
    {"HFWD", 0x51DF73, kReachSabreInHull,
     "F3 0F 10 4E 54 F3 0F 10 46 50 F3 0F 59 C0 F3 0F 59 C9 F3 0F 58 C1 E8 ?? ?? ?? ?? 0F 28 C8 F3 0F 10 46 58"},
};
static_assert(RowsWellFormed(kReachSabreAnchors));
// The dispatcher's own callers, as the drive holds them: [0] the action / kit consumer (an extra call), [1] the
// stick update, [2] the path with no unit. [1] must be proven (it is the update).
inline constexpr FlightSites kReachSabreSites{
    RowIndex(kReachSabreAnchors, "ENTRY"), RowIndex(kReachSabreAnchors, "CALL"), 0x32,
    RowIndex(kReachSabreAnchors, "ENTRY"), 0x80, 3, 7, kReachTlsIndexRva, &kReachCallerAnchors,
    {aim_caller::Caller::Impulse, aim_caller::Caller::Stick, aim_caller::Caller::NoUnit, aim_caller::Caller::Unknown}};
static_assert(Valid(kReachSabreSites));
inline constexpr size_t kReachSabreRows = sizeof(kReachSabreAnchors) / sizeof(kReachSabreAnchors[0]);

struct ReachSabre
{
    uint32_t rows[kReachSabreRows]{};
    Flight flight;
};

inline ReachSabre ResolveReachSabre(Resolver& r)
{
    ReachSabre f;
    f.flight = ResolveFlight(r, kReachSabreAnchors, f.rows, kReachSabreSites);
    return f;
}

// ======================================================================================================
// HALO 3 / ODST / HALO 4: each title's per-user control update.
//
//      title    control update   TLS slot   per-user stride   desired yaw / pitch
//      Halo 3   0xF7DF0          +0xC0      0x110             +0xD4 / +0xD8
//      ODST     0x113AB8         +0xD8      0x2C0             +0xD4 / +0xD8
//      Halo 4   0xA8B34          +0xB0      0x1F0             +0x138 / +0x13C
// ======================================================================================================
// ADDR-HYGIENE: resolved (ResolveLegacyControl, ResolveCallers, ResolveTick). Every RVA in this section is the
// recorded half of a resolution. The TLS slot, stride and angle offsets of the table above stand in the
// patterns' own bytes: a build that changed one stops its pattern matching, and the drive is not hooked.
struct LegacyDriveSite
{
    GameTitle title;
    const char* name;
    uint32_t entryRva;
    const char* prologue;
    bool prologueUnique;        // false: other functions begin the same way (Halo 4: two others)
    uint32_t tlsRva;
    const char* tlsPattern;
    uint32_t accRva;
    const char* accPattern;
    uint32_t tlsIndexLoadRva;   // `mov e?x, [rip+disp32]` (8B 0D / 8B 15) loading the engine TLS index: it sits
                                // inside the prologue's bytes (Halo 3, ODST) or the TLS block's (Halo 4)
    uint32_t tlsSlot;
    uint32_t stride;
    uint32_t angles;
    // The `call entry` (E8 rel32) of the stick update (Halo 3, ODST), or of the dispatcher that every Halo 4
    // caller goes through (its one call site). It is the call inside a row of kLegacyCallerAnchors (asserted
    // below), and is found with that row.
    uint32_t primaryCallRva;
    uint32_t tlsIndexRva;       // where the module's TLS index sits in the recorded build
};

inline constexpr LegacyDriveSite kLegacyDriveSites[3] = {
    {GameTitle::Halo3, "Halo 3", 0xF7DF0,
     "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 F3 0F 11 58 20 55 41 54 41 55 41 56 41 57 48 8D 6C 24 80 "
     "48 81 EC 80 01 00 00 8B 15 ?? ?? ?? ??", true,
     0xF7E65, "65 48 8B 04 25 58 00 00 00 4C 63 E1 49 69 DC 10 01 00 00 48 8B 34 D0 B8 C0 00 00 00 "
              "48 89 74 24 30 48 8B 3C 30",
     0xF8146, "F3 0F 58 B4 3B D4 00 00 00 4C 8B 3D ?? ?? ?? ?? 41 83 C8 FF 4C 8B 35 ?? ?? ?? ?? "
              "B8 38 00 00 00 F3 0F 11 B4 3B D4 00 00 00",
     0xF7E19, 0xC0, 0x110, 0xD4, 0xF6D11, 0x00A39F9C},
    {GameTitle::Halo3ODST, "ODST", 0x113AB8,
     "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 F3 0F 11 58 20 55 41 54 41 55 41 56 41 57 48 8D A8 48 FF "
     "FF FF 48 81 EC 90 01 00 00 8B 15 ?? ?? ?? ??", true,
     0x113B2F, "65 48 8B 04 25 58 00 00 00 4C 63 E1 BF D8 00 00 00 49 69 DC C0 02 00 00 4C 8B 2C D0 "
               "48 8B 05 ?? ?? ?? ?? 4A 8B 3C 2F",
     0x113E0A, "F3 0F 58 B4 1F D4 00 00 00 4C 8B 3D ?? ?? ?? ?? 83 CE FF 4C 8B 35 ?? ?? ?? ?? "
               "B8 20 00 00 00 F3 0F 11 B4 1F D4 00 00 00",
     0x113AE3, 0xD8, 0x2C0, 0xD4, 0x112890, 0x00A8FB1C},
    {GameTitle::Halo4, "Halo 4", 0xA8B34,
     "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 08 FF FF FF "
     "48 81 EC D0 01 00 00", false,
     0xA8BD0, "4C 63 F1 44 0F 28 D9 8B 0D ?? ?? ?? ?? 49 69 DE F0 01 00 00 F3 0F 11 5D 88 4C 8B 3C C8 "
              "48 8B 05 ?? ?? ?? ?? BF B0 00 00 00",
     0xA92A6, "F3 0F 58 84 1F 38 01 00 00 41 0F 28 CD E8 ?? ?? ?? ?? 44 0F 2F D0 F3 0F 11 84 1F 38 01 00 00",
     0xA8BD7, 0xB0, 0x1F0, 0x138, 0xA8988, 0x01057218},
};

// THE CALLER MAP (aim_caller_logic.h). Each site is an exact-byte pattern that must be UNIQUE in the module, and
// whose E8 at callOffset must land on the control update (direct mode: Halo 3, ODST) or on the dispatcher
// (Halo 4); the caller's return address is the found call + 5. Halo 4's dispatcher mode also needs the control
// update's one call site, the dispatcher's prologue (unique) and its frame. THE PROOF LADDER: the stick site is
// rung 1, and without it every call counts as the update; each other site is its own proof, and one that fails is
// simply absent (its calls read "unknown", put back).
inline constexpr CallerAnchors kLegacyCallerAnchors[3] = {
    // Halo 3: the stick update in the dispatcher 0xF66AC; the no-unit path (exclusive with it, 0xF6938);
    // a weapon's pitch kick from 0x366858 during the tick. Only the census and the counters read the last two:
    // every caller that is not the stick is put back.
    {{}, {},
     {{"stick update", "stick update's call", 0xF6D00,
       "F3 0F 10 54 24 5C 0F 28 DE F3 0F 10 4C 24 58 8B CB E8 ?? ?? ?? ??", 0x11, aim_caller::Caller::Stick},
      {"no-unit path", "no-unit path's call", 0xF6F59,
       "F3 0F 10 54 24 5C 0F 28 DE F3 0F 10 4C 24 58 41 8B CF E8 ?? ?? ?? ??", 0x12, aim_caller::Caller::NoUnit},
      {"weapon kick", "weapon kick's call", 0x3675A4,
       "41 0F 28 C8 F3 0F 5C C6 8B CE F3 0F 59 D0 F3 0F 58 D6 E8 ?? ?? ?? ??", 0x12, aim_caller::Caller::Kick},
      {}},
     aim_caller::Action::RestoreOnly},
    // ODST: the same shape (stick update in 0x112184, no-unit path, the weapon's pitch kick).
    {{}, {},
     {{"stick update", "stick update's call", 0x112881,
       "F3 0F 10 55 8B 0F 28 DE F3 0F 10 4D 87 8B CB E8 ?? ?? ?? ?? 41 83 CA FF", 0x0F, aim_caller::Caller::Stick},
      {"no-unit path", "no-unit path's call", 0x112B12,
       "8B 7D 67 0F 28 DE F3 0F 10 55 8B 8B CF F3 0F 10 4D 87 E8 ?? ?? ?? ??", 0x12, aim_caller::Caller::NoUnit},
      {"weapon kick", "weapon kick's call", 0x3ADA90,
       "41 0F 28 C8 F3 0F 5C C6 8B CF F3 0F 59 D0 F3 0F 58 D6 E8 ?? ?? ?? ??", 0x12, aim_caller::Caller::Kick},
      {}},
     aim_caller::Action::RestoreOnly},
    // Halo 4: the control update's one call 0xA8988 in the dispatcher 0xA8828 (frame 0xF8: 5 pushes + 0xD0, no
    // frame register), and the dispatcher's four callers, all in the per-user pass 0xA6BA8 / 0xA2670:
    //   0xA7774 the stick update (the real input &[rbp-0x20]);
    //   0xA349E an effect camera impulse (INFERRED from 0x1456EC's per-player effect slots: damage, explosions);
    //   0xA7751 the weapon's aim SWAY -- not a recoil kick: 0xA5B90 advances two phases from the weapon tag's
    //           per-zoom table (+0x408/+0x40C) and returns sin(phase) x amplitude, every pass it is non-zero;
    //   0xA7A01 the no-unit path (exclusive with the stick, `cmp [pc],-1` at 0xA6FF4).
    {{"control update's call site", "control update's call", 0xA897C,
      "0F 28 DE 41 0F 28 D0 0F 28 CF 8B CE E8 ?? ?? ?? ?? F2 0F 10 43 18 0F 57 ED", 0x0C,
      aim_caller::Caller::Unknown},
     {"dispatcher", 0xA8828, kEntry,
      "48 8B C4 48 89 58 20 55 56 41 54 41 56 41 57 48 8D 68 B1 48 81 EC D0 00 00 00 0F 29 70 C8 0F 29 78 B8 "
      "44 0F 29 40 A8"},
     {{"stick update", "stick update's call", 0xA7756,
       "F3 0F 10 55 F8 4C 8D 4D E0 F3 0F 10 4D F4 8B 4C 24 60 F3 0F 11 7C 24 28 F3 0F 11 74 24 20 "
       "E8 ?? ?? ?? ??", 0x1E, aim_caller::Caller::Stick},
      {"camera impulse", "camera impulse's call", 0xA348D,
       "4C 8D 0D ?? ?? ?? ?? F3 0F 10 95 94 00 00 00 8B CE E8 ?? ?? ?? ??", 0x11, aim_caller::Caller::Impulse},
      {"weapon sway", "weapon sway's call", 0xA7733,
       "0F 57 0D ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 8B 4C 24 60 F3 0F 11 7C 24 28 F3 0F 11 74 24 20 "
       "E8 ?? ?? ?? ??", 0x1E, aim_caller::Caller::Sway},
      {"no-unit path", "no-unit path's call", 0xA79E5,
       "F3 0F 10 55 F8 4C 8D 4D E0 F3 0F 10 4D F4 8B CB F3 0F 11 7C 24 28 F3 0F 11 74 24 20 "
       "E8 ?? ?? ?? ??", 0x1C, aim_caller::Caller::NoUnit}},
     aim_caller::Action::EndUpdate, 0x100},
};
// The primary call each site table records is the call inside one of its own rows.
static_assert(SiteOf(kLegacyCallerAnchors[0], aim_caller::Caller::Stick)->rva +
                  SiteOf(kLegacyCallerAnchors[0], aim_caller::Caller::Stick)->callOffset ==
              kLegacyDriveSites[0].primaryCallRva);
static_assert(SiteOf(kLegacyCallerAnchors[1], aim_caller::Caller::Stick)->rva +
                  SiteOf(kLegacyCallerAnchors[1], aim_caller::Caller::Stick)->callOffset ==
              kLegacyDriveSites[1].primaryCallRva);
static_assert(kLegacyCallerAnchors[2].accumulatorCall.rva + kLegacyCallerAnchors[2].accumulatorCall.callOffset ==
              kLegacyDriveSites[2].primaryCallRva);

// Dev census: each title's sim tick counter. Halo 3 / ODST [[TLS+0xC8]]+0xC, Halo 4 [[TLS+0x90]]+0xC: the
// getter, then the increment inside the per-tick function.
inline constexpr TickAnchors kLegacyTickAnchors[3] = {
    {0xF33B0, "8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 BA C8 00 00 00 48 8B 04 C8 48 8B 0C 10 8B 41 0C C3",
     0xEF60B, "48 8D 54 24 38 48 8D 4C 24 50 E8 ?? ?? ?? ?? 44 84 64 24 54 74 08 48 8B 14 3E 44 01 62 0C", 0xFF, 29},
    {0x10EB40, "8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 BA C8 00 00 00 48 8B 04 C8 48 8B 0C 10 8B 41 0C C3",
     0x10A34A, "48 8D 54 24 38 48 8D 4C 24 50 E8 ?? ?? ?? ?? 8A 44 24 54 24 01 74 07 49 8B 14 1E FF 42 0C", 0xFF, 29},
    {0xA0438, "8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 BA 90 00 00 00 48 8B 04 C8 48 8B 0C 10 8B 41 0C C3",
     0x9ADB9, "44 84 7D 44 74 0D B8 90 00 00 00 48 8B 0C 38 44 01 79 0C", 7, 18},
};

// Halo 4: the record's own unit, player_control +0x120 (0xA8C15 `mov ecx, [rdi+rbx+0x120]`, rdi = TLS+0xB0,
// rbx = user * 0x1F0), from which the hook tells on foot from seated on the sim thread itself. The same seven
// bytes stand three times in the control update, so it is taken only at its recorded offset in that function:
// not there, Halo 4 seats are never written.
// ADDR-HYGIENE: guarded. The offset is the displacement of the instruction below, which ResolveLegacyControl
// finds inside the control update (InFunction) before the hook reads it.
inline constexpr uint32_t kHalo4RecordUnitOffset = 0x120;
inline constexpr uint32_t kHalo4RecordUnitLoadRva = 0xA8C15;
inline constexpr char kHalo4RecordUnitLoad[] = "8B 8C 1F 20 01 00 00";

// THE RECORD'S EASED PITCH LIMITS, Halo 3 and ODST (seat_limit::ResolvePitchLimits). The accumulator eases two
// record fields toward the seat's pitch range every update and clamps the pitch to them. Each site is the clamp's
// exact bytes -- the eased low's store, the eased high's load and store, the pitch's add/max/store -- UNIQUE in
// the module and inside the control update; the six offsets are DECODED from it (`?? ?? 00 00`) and must agree
// (Halo 3 +0x190/+0x194 at 0xF86F1, ODST +0x344/+0x348 at 0x114381). Not bound: a look-steered driver's reticle is
// never clamped in pitch; logged once.
struct LegacyPitchLimitSite
{
    uint32_t rva;
    const char* pattern;
    uint8_t loStore, hiLoad, hiStore, pitchAdd, loMax, pitchStore;   // disp32 positions in the pattern
};
inline constexpr LegacyPitchLimitSite kLegacyPitchLimitSites[2] = {
    {0xF86F1,
     "F3 0F 11 8C 3B ?? ?? 00 00 F3 0F 10 8C 3B ?? ?? 00 00 F3 0F 5C C1 F3 0F 5F C2 44 0F 2F E0 77 04 41 0F 28 C4 "
     "F3 0F 58 C8 41 0F 28 D6 F3 0F 11 8C 3B ?? ?? 00 00 0F 28 C1 F3 0F 58 94 3B ?? ?? 00 00 F3 0F 5F 94 3B ?? ?? 00 00 "
     "0F 2F C2 77 03 0F 28 D1 45 0F 2E F3 F3 0F 11 94 3B ?? ?? 00 00",
     5, 14, 49, 61, 70, 91},
    {0x114381,
     "F3 0F 11 8C 1F ?? ?? 00 00 F3 0F 10 8C 1F ?? ?? 00 00 F3 0F 5C C1 F3 0F 5F C2 0F 2F D8 77 03 0F 28 C3 "
     "F3 0F 58 C8 41 0F 28 D4 F3 0F 11 8C 1F ?? ?? 00 00 0F 28 C1 F3 0F 58 94 1F ?? ?? 00 00 F3 0F 5F 94 1F ?? ?? 00 00 "
     "0F 2F C2 77 03 0F 28 D1 45 0F 2E E3 F3 0F 11 94 1F ?? ?? 00 00",
     5, 14, 47, 59, 68, 89},
};

struct LegacyControl
{
    uint32_t entry = 0, tlsBlock = 0, accumulator = 0;
    uint32_t tlsIndexLoad = 0;     // the instruction that loads the engine TLS index
    uint32_t tlsIndex = 0;         // the module's TLS index, which that instruction loads
    uint32_t recordUnitLoad = 0;   // Halo 4: the record's unit load (0: not proven, seats are never written)
    uint32_t pitchLimit = 0;       // Halo 3, ODST: the accumulator's pitch clamp (0: not bound)
    bool ok = false;               // entry, TLS block, accumulator and TLS index: the hook may exist
};

// `slot`: 0 Halo 3, 1 ODST, 2 Halo 4.
inline LegacyControl ResolveLegacyControl(Resolver& r, int slot)
{
    LegacyControl f;
    const LegacyDriveSite& site = kLegacyDriveSites[slot];
    if (site.prologueUnique)
    {
        f.entry = r.Unique("control update", site.entryRva, site.prologue);
        if (f.entry && !r.IsFunctionStart(f.entry))
            f.entry = r.Refuse(kNotAFunctionStart);
        f.accumulator = r.Unique("accumulator", site.accRva, site.accPattern);
        if (f.accumulator && f.entry && !r.SameFunction(f.accumulator, f.entry))
            f.accumulator = r.Refuse(kNotInItsFunction);
    }
    else
    {
        // Other functions begin with the same bytes: the control update is the function that holds the
        // accumulator, and must begin with them all the same.
        f.accumulator = r.Unique("accumulator", site.accRva, site.accPattern);
        f.entry = r.FunctionStart("control update", site.entryRva, f.accumulator, site.prologue);
    }
    f.tlsBlock = r.Unique("TLS and stride block", site.tlsRva, site.tlsPattern);
    if (f.tlsBlock && f.entry && !r.SameFunction(f.tlsBlock, f.entry))
        f.tlsBlock = r.Refuse(kNotInItsFunction);

    // The TLS index load sits at a fixed place inside the prologue's bytes or the TLS block's.
    drive_resolve::Pattern bytes;
    uint32_t load = 0;
    if (drive_resolve::Compile(site.prologue, bytes) && site.tlsIndexLoadRva >= site.entryRva &&
        site.tlsIndexLoadRva - site.entryRva + 6 <= bytes.length)
        load = f.entry ? f.entry + (site.tlsIndexLoadRva - site.entryRva) : 0;
    else if (drive_resolve::Compile(site.tlsPattern, bytes) && site.tlsIndexLoadRva >= site.tlsRva &&
             site.tlsIndexLoadRva - site.tlsRva + 6 <= bytes.length)
        load = f.tlsBlock ? f.tlsBlock + (site.tlsIndexLoadRva - site.tlsRva) : 0;
    f.tlsIndexLoad = r.Derived("TLS index load", site.tlsIndexLoadRva, load);
    uint8_t opcode[2]{};
    if (f.tlsIndexLoad && !(drive_resolve::ReadOriginal(r.image(), f.tlsIndexLoad, opcode, 2) && opcode[0] == 0x8B &&
                            (opcode[1] == 0x0D || opcode[1] == 0x15)))
        f.tlsIndexLoad = r.Refuse("it is not a load from a rip-relative address (8B 0D / 8B 15)");
    f.tlsIndex = r.TlsIndex("engine TLS index", site.tlsIndexRva,
                            f.tlsIndexLoad ? r.RipTarget(f.tlsIndexLoad, 2, 6) : 0);
    f.ok = f.entry && f.tlsBlock && f.accumulator && f.tlsIndexLoad && f.tlsIndex;

    if (slot == 2)
        // Seven bytes: at its recorded offset or not at all (offsetOnly).
        f.recordUnitLoad = r.InFunction("record unit load", kHalo4RecordUnitLoadRva, site.entryRva, f.accumulator,
                                        kHalo4RecordUnitLoad, true);
    else
    {
        f.pitchLimit = r.Unique("pitch clamp", kLegacyPitchLimitSites[slot].rva, kLegacyPitchLimitSites[slot].pattern);
        if (f.pitchLimit && !(f.entry && r.SameFunction(f.pitchLimit, f.entry)))
            f.pitchLimit = r.Refuse(kNotInItsFunction);
    }
    return f;
}

// ---- A CONTROL UPDATE'S WHOLE PLAN, as its install runs it: the update, its caller map and -- in a dev build
// only (WithTick) -- the sim tick counter the dev census reads. One function for each, run by the install, by the
// tests and by the offline check, so that what those two check is the plan that ships and not one written beside
// it. A player build's plan and a dev build's are two versions of the drive's tables: two lines apart, and
// another plan hash. (The other drives' plans are one function already: ResolveHalo2Control, ResolveReachSabre,
// ResolveHalo4Flight, ResolveT13.) A build that does not ask for the tick has none of its anchors in it.
struct ReachControlPlan
{
    ReachControl control;
    Callers callers;
    Tick tick;
};

template <bool WithTick>
inline ReachControlPlan PlanReachControl(Resolver& r)
{
    ReachControlPlan f;
    f.control = ResolveReachControl(r);
    f.callers = ResolveCallers(r, kReachCallerAnchors, f.control.entry);
    if constexpr (WithTick)
        f.tick = ResolveTick(r, kReachTickAnchors);
    return f;
}

struct LegacyControlPlan
{
    LegacyControl control;
    Callers callers;
    Tick tick;
};

// `slot`: 0 Halo 3, 1 ODST, 2 Halo 4.
template <bool WithTick>
inline LegacyControlPlan PlanLegacyControl(Resolver& r, int slot)
{
    LegacyControlPlan f;
    f.control = ResolveLegacyControl(r, slot);
    f.callers = ResolveCallers(r, kLegacyCallerAnchors[slot], f.control.entry);
    if constexpr (WithTick)
        f.tick = ResolveTick(r, kLegacyTickAnchors[slot]);
    return f;
}

// ---- Halo 4: the Broadsword's pilot. ENTRY, CALL, TLSIDX and PX are found by signature; the rest are the
// instructions that read the fields the drive reads, resolved inside their function.
// The pilot's controller: the function row 0 (ENTRY) is in.
inline constexpr Where kHalo4FlightInController = Within(0, 0xAA524);
// The dispatcher: the function row 1 (CALL) is in.
inline constexpr Where kHalo4FlightInDispatcher = Within(1, 0xA8828);
// The craft's physics: the function row 8 (PX) is in.
inline constexpr Where kHalo4FlightInPhysics = Within(8, 0x6CACD8);
inline constexpr Anchor kHalo4FlightAnchors[] = {
    {"ENTRY", 0xAA524, kEntry,
     "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 38 FF FF FF 48 81 EC 88 01 00 00 0F 29 70 A8 0F 29 78 98 "
     "44 0F 29 40 88 44 0F 29 88 78 FF FF FF 44 0F 29 90 68 FF FF FF 44 0F 29 98 58 FF FF FF 44 0F 29 A0 48 FF FF FF "
     "44 0F 29 A8 38 FF FF FF 44 0F 29 B0 28 FF FF FF 44 0F 29 B8 18 FF FF FF 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 D8 "
     "65 48 8B 04 25 58 00 00 00 48 8D 35 ?? ?? ?? ?? 44 8B CA 41 BC 01 00 00 00"},
    // The dispatcher: the craft's run-time type == 13 -> the controller -> jmp to the post-block.
    {"CALL", 0xA88FA, kAnywhere,
     "8B 4D C7 E8 ?? ?? ?? ?? 83 F8 0D 75 2E F3 0F 10 45 7F 41 0F 28 D8 F3 0F 10 75 77 0F 28 D7 8B 55 C7 8B CE "
     "F3 0F 11 44 24 30 F3 0F 11 74 24 28 4C 89 64 24 20 E8 ?? ?? ?? ?? EB 58"},
    // The dispatcher's object table (TLS + 0x18; an entry's kind byte 1 = a vehicle).
    {"VEH", 0xA88C2, kHalo4FlightInDispatcher,
     "83 7D C7 FF 74 6D 0F B7 45 C7 BA 18 00 00 00 48 8D 0C 40 4A 8B 14 3A 48 8B 42 50 80 7C C8 04 01 75 51"},
    // The TLS index load; player control at TLS + 0xB0.
    {"TLSIDX", 0xAA5AD, MemberOf(0),
     "8B 15 ?? ?? ?? ?? 45 0F 57 C9 41 B8 B0 00 00 00 F3 0F 11 54 24 58 F3 0F 11 5C 24 40 48 8B 14 D0"},
    // The record: *(TLS + 0xB0) + 0x120 + user * 0x1F0 (the direct drive's record + 0x120).
    {"RECSETUP", 0xAA5C9, kHalo4FlightInController,
     "48 8B 14 D0 48 63 C1 48 69 D8 F0 01 00 00 49 8B 04 10 48 05 20 01 00 00"},
    // the Euler output: the controller's record + 0x18
    {"ANGPTR", 0xAA984, kHalo4FlightInController, "4C 8D 7B 18"},
    // The game's own steering: direction vehicle +0x1050 / +0x1058, stamp vehicle +0x104C.
    {"REDIR", 0xAAA3B, kHalo4FlightInController,
     "E8 ?? ?? ?? ?? 83 F8 0D 75 23 F2 41 0F 10 85 50 10 00 00 41 8B 85 58 10 00 00 45 8B B5 4C 10 00 00"},
    // Its window: the tag block's +0xCC against game time (TLS + 0x90: the tick at +0xC, seconds per tick at +8).
    {"WIN", 0xAAB35, kHalo4FlightInController,
     "F3 0F 10 84 BE CC 00 00 00 B8 90 00 00 00 48 8B 0C 08 8B 41 0C 41 2B C6 66 0F 6E C8 0F 5B C9 F3 0F 59 49 08 "
     "0F 2F C1 0F 86 ?? ?? ?? ??"},
    // The craft's physics (0x6CACD8) reading the facing it steers by, vehicle +0x4BC / +0x4C0 / +0x4C4 ...
    {"PX", 0x6CB43E, kAnywhere,
     "F3 41 0F 10 B6 C0 04 00 00 F3 41 0F 10 BE BC 04 00 00 0F 28 C6 0F 28 CF E8 ?? ?? ?? ?? F3 0F 59 FF 44 0F 28 C0 "
     "F3 0F 59 F6 F3 0F 58 FE 0F 28 C7 E8 ?? ?? ?? ?? 0F 28 C8 F3 41 0F 10 86 C4 04 00 00 E8 ?? ?? ?? ?? "
     "F3 41 0F 10 B6 28 05 00 00"},
    // ... and the craft's own forward, vehicle +0x70 / +0x74 (the nose, for the report).
    {"PFWD", 0x6CADEF, kHalo4FlightInPhysics, "F3 41 0F 10 4E 74 F3 41 0F 10 46 70"},
};
static_assert(RowsWellFormed(kHalo4FlightAnchors));
// The dispatcher's own callers, as the drive holds them: [0] the action / kit consumer and [1] the weapon's aim
// sway (extra calls), [2] the stick update, [3] the path with no unit. [2] must be proven (it is the update).
inline constexpr FlightSites kHalo4FlightSites{
    RowIndex(kHalo4FlightAnchors, "ENTRY"), RowIndex(kHalo4FlightAnchors, "CALL"), 0x34,
    RowIndex(kHalo4FlightAnchors, "TLSIDX"), 0, 2, 6, kLegacyDriveSites[2].tlsIndexRva, &kLegacyCallerAnchors[2],
    {aim_caller::Caller::Impulse, aim_caller::Caller::Sway, aim_caller::Caller::Stick, aim_caller::Caller::NoUnit}};
static_assert(Valid(kHalo4FlightSites));
inline constexpr size_t kHalo4FlightRows = sizeof(kHalo4FlightAnchors) / sizeof(kHalo4FlightAnchors[0]);

struct Halo4Flight
{
    uint32_t rows[kHalo4FlightRows]{};
    Flight flight;
};

inline Halo4Flight ResolveHalo4Flight(Resolver& r)
{
    Halo4Flight f;
    f.flight = ResolveFlight(r, kHalo4FlightAnchors, f.rows, kHalo4FlightSites);
    return f;
}

// ======================================================================================================
// HALO 2: the accumulator halo2+0x6C3DF0, void (int user, float dyaw, float dpitch). Its record is
// *[the rip slot the prologue loads] + user * 0xB8: desired yaw +0x38, pitch +0x3C.
// ======================================================================================================
// ADDR-HYGIENE: resolved (ResolveHalo2Control). Every RVA in this section is the recorded half of a resolution.
// Five exact-byte anchors, each unique in the module: the entry prologue (it also carries the record slot's
// disp32), the stride block, the yaw and the pitch accumulators (all inside the accumulator), and the per-user
// control update's call site, whose rel32 must land on the entry (its return address is the only one written
// after).
inline constexpr Anchor kHalo2ControlAnchors[] = {
    {"accumulator", 0x6C3DF0, kEntry,
     "48 8B C4 F3 0F 11 50 18 89 48 08 55 53 57 48 8D A8 C8 FE FF FF 48 81 EC 20 02 00 00 "
     "48 8B 3D ?? ?? ?? ??"},
    {"stride block", 0x6C3E5E, MemberOf(0),
     "48 63 C1 48 69 D8 B8 00 00 00 48 8B 05 ?? ?? ?? ?? 48 63 90 F4 00 00 00"},
    {"yaw accumulator", 0x6C3FF8, MemberOf(0),
     "4C 8D 34 3B F3 44 0F 10 05 ?? ?? ?? ?? 45 0F 57 E4 F2 44 0F 10 0D ?? ?? ?? ?? F3 41 0F 58 76 38 "
     "F3 0F 10 3D ?? ?? ?? ?? 44 0F 29 94 24 B0 01 00 00 44 0F 29 9C 24 A0 01 00 00 F3 41 0F 11 76 38"},
    {"pitch accumulator", 0x6C4740, MemberOf(0),
     "F3 0F 10 8D 50 01 00 00 F3 0F 58 D0 44 0F 28 AC 24 80 01 00 00 F3 0F 11 94 3B AC 00 00 00 "
     "F3 0F 58 4C 3B 3C 0F 2F D9 76 11 F3 0F 11 5C 3B 3C"},
    {"control update's call site", 0x6C0A27, kAnywhere,
     "41 0F BF 4F 28 E8 ?? ?? ?? ?? 84 C0 75 16 41 0F BF 4F 28 F3 0F 10 54 24 44 F3 0F 10 4C 24 40 "
     "E8 ?? ?? ?? ??"},
};
static_assert(RowsWellFormed(kHalo2ControlAnchors));
inline constexpr uint32_t kHalo2ControlCallOffset = 0x1F;       // call site + this: the E8 that calls the accumulator
inline constexpr uint32_t kHalo2ControlSlotLoadOffset = 0x1C;   // entry + this: `mov rdi, [rip+disp32]`, 7 bytes
inline constexpr uint32_t kHalo2ControlSlotRva = 0x15E40D0;     // where that slot sits in the recorded build

struct Halo2Control
{
    uint32_t entry = 0, stride = 0, yaw = 0, pitch = 0, callSite = 0;
    uint32_t call = 0;   // the control update's call of the accumulator (proven to land on the entry)
    uint32_t slot = 0;   // the player control globals slot the prologue loads (inside the module)
    bool ok = false;
};

inline Halo2Control ResolveHalo2Control(Resolver& r)
{
    Halo2Control f;
    uint32_t found[5]{};
    (void)ResolveRows(r, kHalo2ControlAnchors, found);
    f.entry = found[0];
    f.stride = found[1];
    f.yaw = found[2];
    f.pitch = found[3];
    f.callSite = found[4];
    f.call = r.Derived("control update's call", kHalo2ControlAnchors[4].rva + kHalo2ControlCallOffset,
                       f.callSite ? f.callSite + kHalo2ControlCallOffset : 0);
    if (f.call && !(f.entry && r.CallTarget(f.call) == f.entry))
        f.call = r.Refuse("it does not call the accumulator");
    const uint32_t slot = f.entry ? r.RipTarget(f.entry + kHalo2ControlSlotLoadOffset, 3, 7) : 0;
    f.slot = r.Derived("player control globals slot", kHalo2ControlSlotRva,
                       slot && drive_resolve::InImage(r.image(), slot, 8) ? slot : 0);
    f.ok = f.entry && f.stride && f.yaw && f.pitch && f.call && f.slot;
    return f;
}

// ======================================================================================================
// DEV BUILDS ONLY: the log-only observer of the type-13 pilot controllers (src/dll/type13_flight_log.inl). Its
// tables prove every field it reads, the pilot drives' own among them. Nothing in a player build refers to them.
// ======================================================================================================
// ADDR-HYGIENE: dev-only, resolved (ResolveT13). Every RVA in this section is the recorded half of a
// resolution.
// A global the observer reads, named by a rip-relative instruction inside a row.
struct RipSite
{
    int row;
    uint32_t offset;         // the instruction, from the row's start
    uint32_t displacement;   // its disp32
    uint32_t length;
};

// The shared globals, each from independent sites that must agree.
struct T13Sites
{
    RipSite tlsIndex[2];
    RipSite pages[3];
    RipSite instances[3];
};

constexpr bool Valid(const T13Sites& s)
{
    for (const RipSite& site : s.tlsIndex)
        if (site.row < 0)
            return false;
    for (const RipSite& site : s.pages)
        if (site.row < 0)
            return false;
    for (const RipSite& site : s.instances)
        if (site.row < 0)
            return false;
    return true;
}

struct T13
{
    Flight flight;
    uint32_t tlsIndex = 0, pages = 0, instances = 0;   // 0: its sites disagree, or one was not found
    // Every row, the controller's call and the three globals: the observer may be made ready. (The dispatcher's
    // frame and its callers only label the calls it logs.)
    bool ok = false;
};

template <size_t N>
inline uint32_t RipSiteTarget(const Resolver& r, const uint32_t (&found)[N], const RipSite& site)
{
    return found[site.row] ? r.RipTarget(found[site.row] + site.offset, site.displacement, site.length) : 0;
}

template <size_t N>
inline T13 ResolveT13(Resolver& r, const Anchor (&rows)[N], uint32_t (&found)[N], const FlightSites& flight,
                      const T13Sites& s)
{
    T13 f;
    f.flight = ResolveFlight(r, rows, found, flight);
    const uint32_t tls = RipSiteTarget(r, found, s.tlsIndex[0]);
    f.tlsIndex = tls && tls == RipSiteTarget(r, found, s.tlsIndex[1]) && tls == f.flight.tlsIndex ? tls : 0;
    const uint32_t pages = RipSiteTarget(r, found, s.pages[0]);
    f.pages = pages && pages == RipSiteTarget(r, found, s.pages[1]) && pages == RipSiteTarget(r, found, s.pages[2])
        ? pages : 0;
    const uint32_t instances = RipSiteTarget(r, found, s.instances[0]);
    f.instances = instances && instances == RipSiteTarget(r, found, s.instances[1]) &&
                  instances == RipSiteTarget(r, found, s.instances[2]) ? instances : 0;
    f.ok = !f.flight.missing && f.flight.call && f.tlsIndex && f.pages && f.instances;
    return f;
}

// ---- Reach: haloreach+0x64828, the Sabre / Seraph pilot attitude controller.
// The pilot's controller: the function row 0 (ENTRY) is in.
inline constexpr Where kT13ReachInController = Within(0, 0x64828);
// The dispatcher: the function row 1 (CALL) is in.
inline constexpr Where kT13ReachInDispatcher = Within(1, 0x6350C);
// The hull: the function row 33 (HPAGES) is in.
inline constexpr Where kT13ReachInHull = Within(33, 0x51DE3C);
inline constexpr Anchor kT13ReachAnchors[] = {
    // The controller's entry: 151 bytes (the 37-byte prologue alone also matches 0x12B7F0 and 0x15280C).
    // TLS index load at +0x648A8, tag pages lea at +0x648A1.
    {"ENTRY", 0x64828, kEntry,
     "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 18 FF FF FF 48 81 EC A8 01 00 00 0F 29 70 A8 0F 29 78 98 "
     "44 0F 29 40 88 44 0F 29 88 78 FF FF FF 44 0F 29 90 68 FF FF FF 44 0F 29 98 58 FF FF FF 44 0F 29 A0 48 FF FF FF "
     "44 0F 29 A8 38 FF FF FF 44 0F 29 B0 28 FF FF FF 44 0F 29 B8 18 FF FF FF 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 F0 "
     "65 48 8B 04 25 58 00 00 00 48 8D 35 ?? ?? ?? ?? 44 8B 05 ?? ?? ?? ?? 45 0F 57 C9 41 B9 10 00 00 00 41 BD D8 00 00 00"},
    // The dispatcher: physics type == 13 -> call the controller (+0x63607) -> jmp past the accumulator call.
    {"CALL", 0x635D5, kAnywhere,
     "8B 08 E8 ?? ?? ?? ?? 83 F8 0D 75 ?? F3 0F 10 45 7F 0F 28 DF F3 0F 10 4D 77 0F 28 D6 8B 55 C7 8B CF "
     "F3 0F 11 44 24 30 F3 0F 11 4C 24 28 4C 89 6C 24 20 E8 ?? ?? ?? ?? EB ??"},
    // The dispatcher's earlier tests: the camera parent is a vehicle (kind 1) and the seat has flag bit 2.
    {"DRIVER", 0x63592, kT13ReachInDispatcher,
     "E8 ?? ?? ?? ?? 83 7D C7 FF 74 ?? 0F B7 45 C7 B9 10 00 00 00 4A 8B 0C 31 4C 8D 34 40 4C 8B 79 50 43 80 7C F7 04 01 "
     "75 ?? 0F B7 55 CB 41 B8 02 00 00 00 8B 4D C7 E8 ?? ?? ?? ?? 84 C0 74 ??"},
    // The dispatcher's prologue: 7 pushes + sub 0xC0, the caller-attribution frame (DispatcherReturnSlot).
    {"DISP", 0x6350C, kT13ReachInDispatcher,
     "48 8B C4 55 53 57 41 54 41 55 41 56 41 57 48 8D 68 B1 48 81 EC C0 00 00 00"},
    // The record: PC = *(TLS+0xD8) + user*0x180 (lea r12,[rax+rax*2]; shl r12,7); the controller's rbx = PC+0x80.
    {"REC", 0x648B3, kT13ReachInController,
     "41 B9 10 00 00 00 41 BD D8 00 00 00 F3 0F 11 54 24 48 4E 8B 04 C0 48 63 C1 0F B7 CA F3 0F 11 5C 24 50 4F 8B 2C 28 "
     "4C 8D 24 40 4C 89 45 A8 4B 8B 04 01 48 8D 14 49 49 C1 E4 07 48 89 54 24 68 49 8D 9D 80 00 00 00 49 03 DC"},
    // PC+0x88 bit 2: roll mode
    {"PC88", 0x64900, kT13ReachInController, "44 8A 7B 08 48 8B 44 D1 10 48 89 45 A0 41 C0 EF 02"},
    // The tag chain: def index u16 at veh+0; instances (+0x64914); the type-13 block's address at def+0x570.
    {"TAGDEF", 0x64911, kAnywhere,
     "0F B7 08 48 8B 05 ?? ?? ?? ?? 8B 54 C8 04 B9 01 00 00 00 8B C2 48 C1 E8 1C 48 8B 04 C6 8B BC 90 70 05 00 00 "
     "8B C7 48 C1 E8 1C 48 8B 34 C6"},
    // input (entry+0x28) +4: roll axis
    {"ROLLIN", 0x64944, kT13ReachInController, "48 8B 85 10 01 00 00 F3 0F 10 78 04"},
    {"HOLD", 0x6496D, kT13ReachInController, "F3 0F 10 83 C8 00 00 00"},                        // PC+0x148 look hold
    {"LATCH", 0x649E8, kT13ReachInController, "43 88 84 2C 4C 01 00 00"},                       // PC+0x14C latch byte
    {"IDLE", 0x64A34, kT13ReachInController, "89 8B C0 00 00 00 F3 0F 10 43 18"},   // PC+0x140 idle; PC+0x98 pitch
    // sf+0x5C; PC+0x144 roll rate
    {"RRATE", 0x64AD2, kT13ReachInController, "F3 0F 10 64 BE 5C 4C 8D B3 C4 00 00 00"},
    {"SF60", 0x64AF1, kT13ReachInController, "F3 0F 59 5C BE 60"},
    {"SF64", 0x64B0C, kT13ReachInController, "F3 0F 59 5C BE 64"},
    {"SF68", 0x64C2A, kT13ReachInController, "F3 0F 59 5C BE 68"},
    {"SF6C", 0x64A7B, kT13ReachInController, "F3 0F 5D 44 BE 6C"},
    {"SF70", 0x64A51, kT13ReachInController, "0F 2F 4C BE 70"},
    {"SF74", 0x64B89, kT13ReachInController, "F3 0F 5C 4C BE 74"},
    {"SF78", 0x64BAB, kT13ReachInController, "F3 0F 10 44 BE 78"},
    {"SF7C", 0x64A1C, kT13ReachInController, "F3 0F 59 54 BE 7C"},
    {"SF80", 0x64B6B, kT13ReachInController, "F3 0F 10 84 BE 80 00 00 00"},
    {"SF84", 0x64BB7, kT13ReachInController, "F3 0F 10 84 BE 84 00 00 00"},
    {"ANGPTR", 0x64C88, kT13ReachInController, "4C 8D 7B 14"},   // r15 = PC+0x94, the Euler output
    {"ANG", 0x64D19, kT13ReachInController,
     "4C 8D 44 24 58 48 8D 55 C0 48 8D 4D B0 E8 ?? ?? ?? ?? 41 0F 28 D1 48 8D 4C 24 58 49 8B D7 E8 ?? ?? ?? ??"},
    // The forced steer's stamp: direction veh+0xAC0/+0xAC8, tick veh+0xABC.
    {"STAMP", 0x64D6A, kT13ReachInController, "F2 41 0F 10 85 C0 0A 00 00 41 8B 85 C8 0A 00 00 45 8B B5 BC 0A 00 00"},
    {"SFA8", 0x64D9C, kT13ReachInController, "84 94 BE A8 00 00 00"},                           // sf+0xA8 bit 0
    {"AIM", 0x64DA9, kT13ReachInController, "F3 41 0F 10 9D 14 02 00 00"},   // veh+0x214 aiming vector
    // The window: sf+0xAC; game time TLS+0xA0 -> tick +0xC, seconds per tick +8 (comiss/jbe).
    {"WIN", 0x64E58, kT13ReachInController,
     "F3 0F 10 84 BE AC 00 00 00 B9 A0 00 00 00 4A 8B 0C 21 8B 41 0C 41 2B C6 66 0F 6E C8 0F 5B C9 F3 0F 59 49 08 "
     "0F 2F C1 0F 86 ?? ?? ?? ??"},
    {"WRAPP", 0x651E4, kT13ReachInController, "F3 0F 11 43 18"},                                // PC+0x98 pitch wrap
    {"WRAPR", 0x65200, kT13ReachInController, "F3 0F 11 43 1C"},                                // PC+0x9C roll wrap
    {"EXIT", 0x65205, kT13ReachInController,
     "F3 41 0F 10 07 41 0F 28 CE F3 41 0F 58 C6 E8 ?? ?? ?? ?? F3 41 0F 11 07 48 8B 4D F0 48 33 CC"},   // yaw wrap
    // The accumulator 0x6375C adds the stick into the same desired yaw PC+0x94 (direct drive's own anchor).
    {"ACC", 0x63AF9, kAnywhere,
     "0F 28 D0 F3 0F 58 94 1F 94 00 00 00 41 83 CB FF B8 10 00 00 00 F3 0F 11 94 1F 94 00 00 00"},
    // The vehicle-type accessor: 15 twelve-byte type blocks from def+0x4D0 (lea r8,[rdx+0x134]).
    {"TYPEACC", 0x4AC1E4, kAnywhere,
     "48 8B 05 ?? ?? ?? ?? 0F B7 C9 8B 54 C8 04 48 8D 0D ?? ?? ?? ?? 8B C2 48 C1 E8 1C 4C 8D 82 34 01 00 00 48 8B 0C C1 "
     "33 C0 45 33 C9 44 8D 50 10 4E 8D 04 81 41 83 38 00 75 11 FF C0 49 FF C1 49 83 C0 0C 49 83 F9 0F 7C EB EB 03 "
     "44 8B D0 41 8B C2 C3"},
    // The hull 0x51DE3C (the type-13 case of 0x4AD958): pages (+0x51DEB5) and the TLS index (+0x51DEC2) ...
    {"HPAGES", 0x51DEAC, kAnywhere,
     "65 48 8B 04 25 58 00 00 00 48 8D 3D ?? ?? ?? ?? 4C 8B F1 44 8B E2 8B 0D ?? ?? ?? ??"},
    // ... instances
    {"HDEF", 0x51DEF3, kAnywhere, "48 8B 05 ?? ?? ?? ?? 0F B7 0E 0F B7 96 D8 09 00 00 44 8B 6C C8 04"},
    {"HSF", 0x51DF22, kT13ReachInHull, "42 8B 9C A8 70 05 00 00"},   // ... the same block, def+0x570
    {"HFWD", 0x51DF73, kT13ReachInHull,   // veh+0x50/0x54/0x58 (atan2)
     "F3 0F 10 4E 54 F3 0F 10 46 50 F3 0F 59 C0 F3 0F 59 C9 F3 0F 58 C1 E8 ?? ?? ?? ?? 0F 28 C8 F3 0F 10 46 58"},
    // The desired facing the hull steers by, veh+0x1E4/0x1E8/0x1EC, and its roll base veh+0x250.
    {"HX", 0x51E551, kT13ReachInHull,
     "F3 0F 10 B6 E8 01 00 00 F3 0F 10 BE E4 01 00 00 0F 28 C6 0F 28 CF E8 ?? ?? ?? ?? F3 0F 59 FF F3 0F 11 45 C0 "
     "F3 0F 59 F6 F3 0F 58 FE 0F 28 C7 E8 ?? ?? ?? ?? 0F 28 C8 F3 0F 10 86 EC 01 00 00 E8 ?? ?? ?? ?? F3 0F 10 B6 50 02 00 00"},
    {"H10", 0x51E526, kT13ReachInHull, "F3 44 0F 10 54 9F 10"},                           // hull sf+0x10
    {"H14", 0x51E04D, kT13ReachInHull, "F3 44 0F 59 54 9F 14"},                           // hull sf+0x14
    {"H38", 0x51E7CE, kT13ReachInHull, "F3 0F 10 64 9F 38 F3 0F 59 A6 A0 03 00 00"},      // hull sf+0x38 x veh+0x3A0
    {"H88", 0x51E5F7, kT13ReachInHull, "F3 0F 59 84 9F 88 00 00 00"},
    {"H8C", 0x51E5DC, kT13ReachInHull, "F3 0F 10 9C 9F 8C 00 00 00"},
    {"H90", 0x51E62A, kT13ReachInHull, "F3 0F 10 84 9F 90 00 00 00"},
    // Hull steering is skipped while veh + i16[veh+0x1AE] holds 0x1B.
    {"STATE", 0x51FDE0, kAnywhere, "48 0F BF 81 AE 01 00 00 48 03 C1 74 07 83 38 1B 41 0F 94 C1"},
};
static_assert(RowsWellFormed(kT13ReachAnchors));
inline constexpr FlightSites kT13ReachFlight{
    RowIndex(kT13ReachAnchors, "ENTRY"), RowIndex(kT13ReachAnchors, "CALL"), 0x32,
    RowIndex(kT13ReachAnchors, "ENTRY"), 0x80, 3, 7, kReachTlsIndexRva, &kReachCallerAnchors,
    {aim_caller::Caller::Impulse, aim_caller::Caller::Stick, aim_caller::Caller::NoUnit, aim_caller::Caller::Unknown}};
// TLS index (ENTRY, HPAGES), tag pages (ENTRY, HPAGES, TYPEACC), tag instances (TAGDEF, HDEF, TYPEACC).
inline constexpr T13Sites kT13ReachSites{
    {{RowIndex(kT13ReachAnchors, "ENTRY"), 0x80, 3, 7}, {RowIndex(kT13ReachAnchors, "HPAGES"), 0x16, 2, 6}},
    {{RowIndex(kT13ReachAnchors, "ENTRY"), 0x79, 3, 7}, {RowIndex(kT13ReachAnchors, "HPAGES"), 0x09, 3, 7},
     {RowIndex(kT13ReachAnchors, "TYPEACC"), 0x0E, 3, 7}},
    {{RowIndex(kT13ReachAnchors, "TAGDEF"), 0x03, 3, 7}, {RowIndex(kT13ReachAnchors, "HDEF"), 0x00, 3, 7},
     {RowIndex(kT13ReachAnchors, "TYPEACC"), 0x00, 3, 7}}};
static_assert(Valid(kT13ReachFlight) && Valid(kT13ReachSites));
inline constexpr size_t kT13ReachRows = sizeof(kT13ReachAnchors) / sizeof(kT13ReachAnchors[0]);

// ---- Halo 4: halo4+0xAA524, the Broadsword pilot attitude controller.
// The pilot's controller: the function row 0 (ENTRY) is in.
inline constexpr Where kT13Halo4InController = Within(0, 0xAA524);
// The dispatcher: the function row 1 (CALL) is in.
inline constexpr Where kT13Halo4InDispatcher = Within(1, 0xA8828);
// The craft's physics: the function row 32 (PPAGES) is in.
inline constexpr Where kT13Halo4InPhysics = Within(32, 0x6CACD8);
// The control update: the function row 47 (ACCUM) is in.
inline constexpr Where kT13Halo4InControlUpdate = Within(47, 0xA8B34);
inline constexpr Anchor kT13Halo4Anchors[] = {
    // The controller's entry: 137 bytes (the short prologue alone also matches 0x7BE124). Pages lea +0xAA59D.
    {"ENTRY", 0xAA524, kEntry,
     "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 38 FF FF FF 48 81 EC 88 01 00 00 0F 29 70 A8 0F 29 78 98 "
     "44 0F 29 40 88 44 0F 29 88 78 FF FF FF 44 0F 29 90 68 FF FF FF 44 0F 29 98 58 FF FF FF 44 0F 29 A0 48 FF FF FF "
     "44 0F 29 A8 38 FF FF FF 44 0F 29 B0 28 FF FF FF 44 0F 29 B8 18 FF FF FF 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 D8 "
     "65 48 8B 04 25 58 00 00 00 48 8D 35 ?? ?? ?? ?? 44 8B CA 41 BC 01 00 00 00"},
    // The dispatcher: runtime type (0x63EC30) == 13 -> call the controller (+0xA892E) -> jmp 0xA898D.
    {"CALL", 0xA88FA, kAnywhere,
     "8B 4D C7 E8 ?? ?? ?? ?? 83 F8 0D 75 2E F3 0F 10 45 7F 41 0F 28 D8 F3 0F 10 75 77 0F 28 D7 8B 55 C7 8B CE "
     "F3 0F 11 44 24 30 F3 0F 11 74 24 28 4C 89 64 24 20 E8 ?? ?? ?? ?? EB 58"},
    // The dispatcher's object table (TLS+0x18, kind 1 = vehicle) and seat flag bit 2 tests, and its post-block.
    {"VEH", 0xA88C2, kT13Halo4InDispatcher,
     "83 7D C7 FF 74 6D 0F B7 45 C7 BA 18 00 00 00 48 8D 0C 40 4A 8B 14 3A 48 8B 42 50 80 7C C8 04 01 75 51"},
    {"DRIVER", 0xA88E4, kT13Halo4InDispatcher, "0F B7 55 CB 41 B8 02 00 00 00 8B 4D C7 E8 ?? ?? ?? ?? 84 C0 74 3B"},
    {"JOIN", 0xA898D, kT13Halo4InDispatcher,
     "F2 0F 10 43 18 0F 57 ED F3 0F 10 25 ?? ?? ?? ?? 8B 43 20 F3 44 0F 10 05 ?? ?? ?? ?? F2 0F 11 45 A7"},
    // The dispatcher's prologue: 5 pushes + sub 0xD0 (mov [rax+20h],rbx moves nothing).
    {"DISP", 0xA8828, kT13Halo4InDispatcher,
     "48 8B C4 48 89 58 20 55 56 41 54 41 56 41 57 48 8D 68 B1 48 81 EC D0 00 00 00"},
    // The TLS index load (must be the one the camera core resolved); player_control at TLS+0xB0.
    {"TLSIDX", 0xAA5AD, MemberOf(0),
     "8B 15 ?? ?? ?? ?? 45 0F 57 C9 41 B8 B0 00 00 00 F3 0F 11 54 24 58 F3 0F 11 5C 24 40 48 8B 14 D0"},
    // The record: *(TLS+0xB0) + 0x120 + user*0x1F0 (the legacy-drive record + 0x120; its angles at +0x18).
    {"RECSETUP", 0xAA5C9, kT13Halo4InController,
     "48 8B 14 D0 48 63 C1 48 69 D8 F0 01 00 00 49 8B 04 10 48 05 20 01 00 00"},
    // rec+0x08 bit 2: roll mode
    {"PC08", 0xAA5F6, kT13Halo4InController, "44 8A 7B 08 4D 8B 04 10 48 8D 14 49 41 C0 EF 02"},
    // Instances; def index u16 at veh+8; the type-13 block (T) at def+0x670.
    {"TAGDEF", 0xAA60F, kAnywhere,
     "48 8B 05 ?? ?? ?? ?? 41 0F B7 4D 08 8B 54 C8 04 8B C2 48 C1 E8 1C 48 8B 04 C6 8B BC 90 70 06 00 00 8B C7 48 C1 "
     "E8 1C 48 8B 34 C6"},
    {"ROLLIN", 0xAA63F, kT13Halo4InController, "48 8B 85 F0 00 00 00 F3 0F 10 70 04"},   // input (entry+0x28) +4
    {"HOLD", 0xAA668, kT13Halo4InController, "F3 0F 10 83 20 01 00 00"},                        // rec+0x120 look hold
    {"LATCH", 0xAA6E5, kT13Halo4InController, "88 83 24 01 00 00"},   // rec+0x124 latch byte
    {"IDLE", 0xAA731, kT13Halo4InController, "89 8B 18 01 00 00"},   // rec+0x118 idle timer
    // T+0x60; rec+0x11C roll rate
    {"RRATE", 0xAA7D1, kT13Halo4InController, "F3 0F 10 64 BE 60 4C 8D B3 1C 01 00 00"},
    {"T64", 0xAA7F1, kT13Halo4InController, "F3 0F 59 5C BE 64"},
    {"T68", 0xAA80D, kT13Halo4InController, "F3 0F 59 5C BE 68"},
    {"T6C", 0xAA925, kT13Halo4InController, "F3 0F 59 5C BE 6C"},
    {"T70", 0xAA77B, kT13Halo4InController, "F3 0F 5D 44 BE 70"},
    {"T74", 0xAA750, kT13Halo4InController, "0F 2F 4C BE 74"},
    {"T78", 0xAA88A, kT13Halo4InController, "F3 0F 5C 4C BE 78"},
    {"T7C", 0xAA8AC, kT13Halo4InController, "F3 0F 10 44 BE 7C"},
    {"T80", 0xAA717, kT13Halo4InController, "F3 0F 59 94 BE 80 00 00 00"},
    {"T84", 0xAA86C, kT13Halo4InController, "F3 0F 10 84 BE 84 00 00 00"},
    {"T88", 0xAA8B8, kT13Halo4InController, "F3 0F 10 84 BE 88 00 00 00"},
    {"ANGPTR", 0xAA984, kT13Halo4InController, "4C 8D 7B 18"},   // r15 = rec+0x18, the Euler output
    {"COMPOSE", 0xAAA03, kT13Halo4InController,
     "48 8D 54 24 48 48 8D 4D A8 E8 ?? ?? ?? ?? 4C 8D 44 24 48 48 8D 55 A8 48 8D 4D 98 E8 ?? ?? ?? ?? 41 0F 28 D1 "
     "48 8D 4C 24 48 49 8B D7 E8 ?? ?? ?? ??"},
    // The redirect: direction veh+0x1050/+0x1058, stamp veh+0x104C; T+0xC8 bit 0.
    {"REDIR", 0xAAA3B, kT13Halo4InController,
     "E8 ?? ?? ?? ?? 83 F8 0D 75 23 F2 41 0F 10 85 50 10 00 00 41 8B 85 58 10 00 00 45 8B B5 4C 10 00 00"},
    {"TC8", 0xAAA72, kT13Halo4InController, "F6 84 BE C8 00 00 00 01"},
    // The window: T+0xCC; game time TLS+0x90 -> tick +0xC, seconds per tick +8.
    {"WIN", 0xAAB35, kT13Halo4InController,
     "F3 0F 10 84 BE CC 00 00 00 B8 90 00 00 00 48 8B 0C 08 8B 41 0C 41 2B C6 66 0F 6E C8 0F 5B C9 F3 0F 59 49 08 "
     "0F 2F C1 0F 86 ?? ?? ?? ??"},
    {"YAWWRAP", 0xAAEF3, kT13Halo4InController,
     "F3 41 0F 10 07 41 0F 28 CD F3 41 0F 58 C5 E8 ?? ?? ?? ?? F3 41 0F 11 07 48 8B 4D D8 48 33 CC"},
    // The redirect's writer 0x630EE8: veh+0x104C = tick, +0x1050/+0x1058 = direction, byte +0x105C = 1.
    {"REDIRW", 0x631014, kAnywhere,
     "83 F8 0D 75 41 B8 90 00 00 00 48 8B 0C 30 8B 41 0C 41 89 86 4C 10 00 00 48 8B 44 24 30 F2 0F 10 00 "
     "F2 41 0F 11 86 50 10 00 00 8B 40 08 41 89 86 58 10 00 00 41 C6 86 5C 10 00 00 01"},
    // The physics 0x6CACD8 (case 13 of 0x6425BC): pages (+0x6CAD51), the TLS index (+0x6CAD5E), instances
    // (+0x6CAD8A), the same block def+0x670, veh+0x70/0x74 (atan2), the facing it steers by and the roll.
    {"PPAGES", 0x6CAD48, kAnywhere,
     "65 48 8B 04 25 58 00 00 00 48 8D 3D ?? ?? ?? ?? 44 8B E2 48 8B F1 8B 15 ?? ?? ?? ?? 4D 8B E8 41 0F B7 CC "
     "41 B8 18 00 00 00 48 8B 04 D0 48 89 45 E8 48 8D 14 49 4D 8B 04 00 49 8B 40 50 4C 8B 74 D0 10 48 8B 05 ?? ?? ?? ?? "
     "41 0F B7 4E 08 44 8B 7C C8 04"},
    {"PT", 0x6CADAE, kT13Halo4InPhysics, "42 8B 9C B8 70 06 00 00"},
    {"PFWD", 0x6CADEF, kT13Halo4InPhysics, "F3 41 0F 10 4E 74 F3 41 0F 10 46 70"},
    {"PX", 0x6CB43E, kT13Halo4InPhysics,
     "F3 41 0F 10 B6 C0 04 00 00 F3 41 0F 10 BE BC 04 00 00 0F 28 C6 0F 28 CF E8 ?? ?? ?? ?? F3 0F 59 FF 44 0F 28 C0 "
     "F3 0F 59 F6 F3 0F 58 FE 0F 28 C7 E8 ?? ?? ?? ?? 0F 28 C8 F3 41 0F 10 86 C4 04 00 00 E8 ?? ?? ?? ?? "
     "F3 41 0F 10 B6 28 05 00 00"},
    {"P10", 0x6CB426, kT13Halo4InPhysics, "F3 44 0F 10 64 9F 10"},                           // physics T+0x10
    {"P14", 0x6CAEDA, kT13Halo4InPhysics, "F3 44 0F 59 5C 9F 14"},                           // physics T+0x14
    {"P3C", 0x6CB864, kT13Halo4InPhysics, "F3 0F 10 64 9F 3C F3 41 0F 59 A6 A4 06 00 00"},   // T+0x3C x veh+0x6A4
    {"PA4", 0x6CB4B7, kT13Halo4InPhysics, "F6 84 9F A4 00 00 00 01"},                        // T+0xA4 bit 0
    {"PA8", 0x6CB52B, kT13Halo4InPhysics, "F3 0F 59 B4 9F A8 00 00 00"},                     // T+0xA8
    // The biped physics mode *(i32*)(unit + u16[unit+0x47A]) (7/8 rebase the record's angles).
    {"PHYSMODE", 0x80C91, kAnywhere, "0F B7 81 7A 04 00 00 8B 04 08 83 C0 F9 83 F8 01 0F 87"},
    // The runtime type: veh+0xF58 set -> (i8) veh+0xF59, else the tag accessor 0x62DAD0.
    {"TYPEOVR", 0x63EC48, kAnywhere, "80 B8 58 0F 00 00 00 74 09 0F BE 98 59 0F 00 00 EB 0A 8B 48 08 E8"},
    {"TAGTYPE", 0x62DAD0, kAnywhere,
     "48 8B 05 ?? ?? ?? ?? 0F B7 C9 8B 54 C8 04 48 8D 0D ?? ?? ?? ?? 8B C2 48 C1 E8 1C 4C 8D 82 74 01 00 00 48 8B 0C C1 "
     "33 C0 45 33 C9 44 8D 50 10 4E 8D 04 81 41 83 38 00 75 11 FF C0 49 FF C1 49 83 C0 0C 49 83 F9 0F 7C EB EB 03 "
     "44 8B D0 41 8B C2 C3"},
    // The driver -> vehicle facing copy 0x5EE590: skipped while veh+0x16C bit 2; reads the driver unit's
    // +0x4C4/+0x4BC and writes the vehicle's.
    {"GATE16C", 0x5EEAC1, kAnywhere, "83 F8 FF 0F 84 ?? ?? ?? ?? 8B 8E 6C 01 00 00 C1 E9 02 80 E1 01 0F 85"},
    {"UNIT4BC", 0x5EEB76, kAnywhere, "45 8B 84 24 C4 04 00 00 F2 45 0F 10 94 24 BC 04 00 00"},
    {"COPY4BC", 0x5EEF09, kAnywhere, "F2 44 0F 11 96 BC 04 00 00 44 89 86 C4 04 00 00 41 8B 84 24 28 05 00 00"},
    // The control update's own add of the stick into the desired yaw (the direct drive's anchor): it locates the
    // function RECUNIT sits in.
    {"ACCUM", 0xA92A6, kAnywhere,
     "F3 0F 58 84 1F 38 01 00 00 41 0F 28 CD E8 ?? ?? ?? ?? 44 0F 2F D0 F3 0F 11 84 1F 38 01 00 00"},
    // The record's unit at player_control +0x120 (= the controller record +0; direct drive's own anchor).
    {"RECUNIT", 0xA8C15, kT13Halo4InControlUpdate, "8B 8C 1F 20 01 00 00"},
};
static_assert(RowsWellFormed(kT13Halo4Anchors));
inline constexpr FlightSites kT13Halo4Flight{
    RowIndex(kT13Halo4Anchors, "ENTRY"), RowIndex(kT13Halo4Anchors, "CALL"), 0x34,
    RowIndex(kT13Halo4Anchors, "TLSIDX"), 0, 2, 6, kLegacyDriveSites[2].tlsIndexRva, &kLegacyCallerAnchors[2],
    {aim_caller::Caller::Impulse, aim_caller::Caller::Sway, aim_caller::Caller::Stick, aim_caller::Caller::NoUnit}};
// TLS index (TLSIDX, PPAGES), tag pages (ENTRY, TAGTYPE, PPAGES), tag instances (TAGDEF, TAGTYPE, PPAGES).
inline constexpr T13Sites kT13Halo4Sites{
    {{RowIndex(kT13Halo4Anchors, "TLSIDX"), 0x00, 2, 6}, {RowIndex(kT13Halo4Anchors, "PPAGES"), 0x16, 2, 6}},
    {{RowIndex(kT13Halo4Anchors, "ENTRY"), 0x79, 3, 7}, {RowIndex(kT13Halo4Anchors, "TAGTYPE"), 0x0E, 3, 7},
     {RowIndex(kT13Halo4Anchors, "PPAGES"), 0x09, 3, 7}},
    {{RowIndex(kT13Halo4Anchors, "TAGDEF"), 0x00, 3, 7}, {RowIndex(kT13Halo4Anchors, "TAGTYPE"), 0x00, 3, 7},
     {RowIndex(kT13Halo4Anchors, "PPAGES"), 0x42, 3, 7}}};
static_assert(Valid(kT13Halo4Flight) && Valid(kT13Halo4Sites));
inline constexpr size_t kT13Halo4Rows = sizeof(kT13Halo4Anchors) / sizeof(kT13Halo4Anchors[0]);
} // namespace drive_sites
