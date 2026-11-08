#pragma once

// AIM CALLER SPLIT -- which caller ran the aim accumulator this time, and what that call may do.
//
// Every title's direct drive hooks the ACCUMULATOR of player_control.desired_angles (desired += stick delta,
// then the clamps) and writes the hand's aim right after it. The accumulator does not only run for the stick:
//   * Halo 3 / ODST call it directly from three sites -- the stick update, the no-unit path and a weapon's
//     pitch kick -- so the hook's own return address names the caller (the primary-return filter);
//   * Halo 4 and Reach call it from ONE site inside a per-user DISPATCHER (H4 0xA8828, Reach 0x6350C), and the
//     dispatcher is called several times in one control pass: [effect camera impulse] -> [weapon sway, Halo 4]
//     -> the stick update, or [impulse] -> the no-unit path. Every call returns to the same place, so the
//     hook's return address names nothing; the DISPATCHER's return address does. Both dispatchers keep a
//     fixed 0xF8-byte frame with no frame register (their UNWIND_INFO), so it sits at accumulator entry rsp +
//     0x100 (measured by static analysis of the retail Steam modules).
// The control pass runs once per MAIN-LOOP ITERATION (a rendered frame), ahead of that iteration's 0..N sim
// ticks, so "per tick" is the wrong key for anything: at 120 fps over a 60 Hz sim there are two legitimate
// stick updates per tick, and a frame with no tick still updates. The right key is the caller:
//   * the STICK call is the update: it steps every check and writes;
//   * an extra call (impulse, sway, kick, or an unrecognised caller once the stick site is proven) puts back
//     the last stick write on the axes it wrote, so the next stick call reads an unchanged record -- the seat
//     latch never takes an impulse for a seat limit, and the stint check is never judged on a kicked value;
//   * the NO-UNIT call ends the update: nothing was written, so the latch and the seat stint are over.
// Every proof that fails falls back to the title's behaviour without this split. Only an unproven STICK site
// means "every call is the update"; a missing extra site just reads as an unrecognised caller.
//
// Zero engine dependencies; unit-tested in tests/aim_caller_tests.cpp.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace aim_caller {

enum class Caller : uint8_t { Stick, Impulse, Sway, Kick, NoUnit, Unknown };
constexpr int kCallerCount = 6;

inline const char* CallerName(Caller c)
{
    switch (c) {
    case Caller::Stick: return "stick";
    case Caller::Impulse: return "impulse";
    case Caller::Sway: return "sway";
    case Caller::Kick: return "kick";
    case Caller::NoUnit: return "no-unit";
    default: return "unknown";
    }
}

enum class Action : uint8_t {
    StepAndWrite,   // the update: every check steps, the setpoint is written (the hook's body as before)
    RestoreOnly,    // an extra call: put the last stick write back over what it added; step and count nothing
    EndUpdate,      // the no-unit path: nothing is written this update -- the "nothing written" epilogue only
};

struct Site {
    uintptr_t ret = 0;
    Caller role = Caller::Unknown;
};

constexpr int kMaxSites = 6;
// A dispatcher frame is read off the stack: never trust one outside this window.
constexpr uint32_t kMinDispatcherFrame = 0x10;
constexpr uint32_t kMaxDispatcherFrame = 0x1000;

// Which return address names which caller. DIRECT mode (acc_ret == 0): the hook's own return address.
// DISPATCHER mode: the hook's return address must be the accumulator's one call site (acc_ret), and the
// dispatcher's return address -- read at accumulator entry rsp + dispatcher_frame -- names the caller.
// Written once at install, before the hook is enabled (a barrier); rewritten only after the hook's removal
// has drained.
struct CallerMap {
    uintptr_t acc_ret = 0;
    uint32_t dispatcher_frame = 0;
    Site site[kMaxSites]{};
    uint8_t count = 0;
    // H3/ODST keep their no-unit calls on the put-back they always had; H4/Reach end the update there.
    Action no_unit_action = Action::EndUpdate;

    void clear()
    {
        acc_ret = 0;
        dispatcher_frame = 0;
        count = 0;
        no_unit_action = Action::EndUpdate;
    }

    bool dispatcher_mode() const { return acc_ret != 0; }

    // A site is recorded once; a second role for the same return address is refused (a broken table).
    bool add(uintptr_t ret, Caller role)
    {
        if (!ret || role == Caller::Unknown || count >= kMaxSites) return false;
        for (int i = 0; i < count; ++i)
            if (site[i].ret == ret) return site[i].role == role;
        site[count].ret = ret;
        site[count].role = role;
        ++count;
        return true;
    }

    Caller lookup(uintptr_t ret) const
    {
        if (!ret) return Caller::Unknown;
        for (int i = 0; i < count; ++i)
            if (site[i].ret == ret) return site[i].role;
        return Caller::Unknown;
    }

    bool has(Caller role) const
    {
        for (int i = 0; i < count; ++i)
            if (site[i].role == role) return true;
        return false;
    }

    // RUNG 1 of the proof ladder: the stick site is known (and, in dispatcher mode, the frame through which
    // its return is read). Without it every call is the update, exactly as before the split.
    bool stick_proven() const
    {
        if (!has(Caller::Stick)) return false;
        if (!dispatcher_mode()) return true;
        return dispatcher_frame >= kMinDispatcherFrame && dispatcher_frame <= kMaxDispatcherFrame &&
               (dispatcher_frame & 7u) == 0;
    }

    // `ret`: the hook's own return address. `dispatcher_ret`: what DispatcherReturn read (0 = none).
    Caller classify(uintptr_t ret, uintptr_t dispatcher_ret) const
    {
        if (!dispatcher_mode()) return lookup(ret);
        if (ret != acc_ret) return Caller::Unknown;
        return lookup(dispatcher_ret);
    }
};

// Dispatcher mode: the dispatcher's return address, read at the accumulator's entry rsp (the slot holding the
// hook's own return address) + the proven frame. 0 when not in dispatcher mode, or the hook was not entered
// from the accumulator's own call site. Reads memory: the caller guards it (SEH in the hook).
inline uintptr_t DispatcherReturn(const CallerMap& m, const uint8_t* entry_rsp, uintptr_t ret)
{
    if (!entry_rsp || !m.stick_proven() || !m.dispatcher_mode() || ret != m.acc_ret) return 0;
    uintptr_t value = 0;
    std::memcpy(&value, entry_rsp + m.dispatcher_frame, sizeof(value));
    return value;
}

// THE PROOF LADDER. Rung 1 (the stick site) decides everything; extra sites only refine the census and the
// action per role. Once the stick is proven, nothing else can be the stick call, so an unrecognised caller is
// put back rather than taken for the update (a partly failed map, or a foreign hook on the dispatcher, must
// never bring the double update back).
inline Action ActionFor(const CallerMap& m, Caller c)
{
    if (!m.stick_proven()) return Action::StepAndWrite;
    switch (c) {
    case Caller::Stick: return Action::StepAndWrite;
    case Caller::NoUnit: return m.no_unit_action;
    default: return Action::RestoreOnly;
    }
}

// The fixed frame of a function with no frame register, from its x64 UNWIND_INFO: the bytes its prolog
// pushes and allocates. `len`: how many bytes of `ui` are readable. Accepts version 1, EHANDLER/UHANDLER;
// refuses CHAININFO (the frame would continue in another entry), a frame register, SET_FPREG, PUSH_MACHFRAME
// and any op it does not know. SAVE_NONVOL / SAVE_XMM128 (and their FAR forms) store into the frame without
// moving rsp: skipped by their exact slot widths (2, 3). `prolog_size` (optional): SizeOfProlog.
inline bool FrameFromUnwindInfo(const uint8_t* ui, size_t len, uint32_t& frame, uint8_t* prolog_size = nullptr)
{
    frame = 0;
    if (!ui || len < 4) return false;
    const uint8_t version = ui[0] & 7u, flags = ui[0] >> 3;
    if (version != 1 || (flags & 4u) || (ui[3] & 0x0Fu)) return false;
    const unsigned count = ui[2];
    if (4u + count * 2u > len) return false;
    const uint8_t* codes = ui + 4;
    const auto slot16 = [codes](unsigned i) {
        return static_cast<uint32_t>(codes[2 * i]) | (static_cast<uint32_t>(codes[2 * i + 1]) << 8);
    };
    uint32_t total = 0;
    for (unsigned i = 0; i < count;) {
        const uint8_t op = codes[2 * i + 1] & 0x0Fu, info = codes[2 * i + 1] >> 4;
        switch (op) {
        case 0:   // UWOP_PUSH_NONVOL
            total += 8;
            i += 1;
            break;
        case 1:   // UWOP_ALLOC_LARGE
            if (info == 0) {
                if (i + 1 >= count) return false;
                total += slot16(i + 1) * 8u;
                i += 2;
            } else if (info == 1) {
                if (i + 2 >= count) return false;
                total += slot16(i + 1) | (slot16(i + 2) << 16);
                i += 3;
            } else {
                return false;
            }
            break;
        case 2:   // UWOP_ALLOC_SMALL
            total += info * 8u + 8u;
            i += 1;
            break;
        case 4:   // UWOP_SAVE_NONVOL
        case 8:   // UWOP_SAVE_XMM128
            if (i + 1 >= count) return false;
            i += 2;
            break;
        case 5:   // UWOP_SAVE_NONVOL_FAR
        case 9:   // UWOP_SAVE_XMM128_FAR
            if (i + 2 >= count) return false;
            i += 3;
            break;
        default:  // 3 SET_FPREG, 6/7 (v2 epilog, spare), 10 PUSH_MACHFRAME, anything else
            return false;
        }
        if (total > 0x100000u) return false;
    }
    if (!total) return false;
    frame = total;
    if (prolog_size) *prolog_size = ui[1];
    return true;
}

// The same frame decoded from the prologue's own instructions, as a cross-check of the unwind data. Only the
// forms an MSVC frame-pointer-less prologue uses before its stack allocation are accepted:
//   mov rax, rsp (48 8B C4); mov [rax+d8], r64 (48/4C 89 /r, mod 01 rm rax); push r64 (50+r, 41 50+r);
//   lea r64, [rax+d8|d32] (48/4C 8D /r, mod 01|10 rm rax); and it ends at sub rsp, imm32|imm8
//   (48 81 EC / 48 83 EC). Anything else is refused.
inline bool FrameFromPrologue(const uint8_t* p, size_t len, uint32_t& frame)
{
    frame = 0;
    if (!p) return false;
    uint32_t pushes = 0;
    for (size_t i = 0; i < len;) {
        const uint8_t b = p[i];
        if (b >= 0x50 && b <= 0x57) { pushes += 8; i += 1; continue; }
        if (b == 0x41 && i + 1 < len && p[i + 1] >= 0x50 && p[i + 1] <= 0x57) { pushes += 8; i += 2; continue; }
        if (b != 0x48 && b != 0x4C) return false;
        if (i + 2 >= len) return false;
        const uint8_t opc = p[i + 1], modrm = p[i + 2];
        if (b == 0x48 && opc == 0x8B && modrm == 0xC4) { i += 3; continue; }                      // mov rax, rsp
        if (opc == 0x89 && (modrm & 0xC7u) == 0x40u) { if (i + 4 > len) return false; i += 4; continue; }
        if (opc == 0x8D && (modrm & 0xC7u) == 0x40u) { if (i + 4 > len) return false; i += 4; continue; }
        if (opc == 0x8D && (modrm & 0xC7u) == 0x80u) { if (i + 7 > len) return false; i += 7; continue; }
        if (b == 0x48 && opc == 0x81 && modrm == 0xEC) {
            if (i + 7 > len) return false;
            const uint32_t imm = static_cast<uint32_t>(p[i + 3]) | (static_cast<uint32_t>(p[i + 4]) << 8) |
                                 (static_cast<uint32_t>(p[i + 5]) << 16) | (static_cast<uint32_t>(p[i + 6]) << 24);
            if (imm > 0x100000u) return false;
            frame = pushes + imm;
            return frame != 0;
        }
        if (b == 0x48 && opc == 0x83 && modrm == 0xEC) {
            if (i + 4 > len) return false;
            frame = pushes + p[i + 3];
            return frame != 0;
        }
        return false;
    }
    return false;
}

// Dispatcher-mode install proof, engine-free: given the module image and the facts its .pdata gives for the
// dispatcher (the RUNTIME_FUNCTION that contains it, looked up by the caller), prove the frame through which
// the dispatcher's return address is read from the accumulator's entry rsp. All of:
//   * the RUNTIME_FUNCTION begins at the dispatcher and contains the accumulator's call (whole, past the
//     prolog);
//   * its UNWIND_INFO decodes to a fixed frame (FrameFromUnwindInfo);
//   * the prologue's own instructions decode to the same frame (FrameFromPrologue).
// `entry_frame`: that frame + 8 (the call into the accumulator pushed one return address). `why`: the first
// failing proof.
inline bool ProveDispatcherFrame(const uint8_t* image, size_t size, uint32_t dispatcher_rva, uint32_t begin_rva,
                                 uint32_t end_rva, uint32_t unwind_rva, uint32_t acc_call_rva,
                                 uint32_t& entry_frame, const char*& why)
{
    entry_frame = 0;
    why = nullptr;
    if (!image || dispatcher_rva >= size || unwind_rva >= size) { why = "dispatcher outside the module"; return false; }
    if (begin_rva != dispatcher_rva || end_rva <= begin_rva || end_rva > size) {
        why = "dispatcher .pdata entry";
        return false;
    }
    if (acc_call_rva < begin_rva || acc_call_rva + 5u > end_rva) { why = "accumulator call outside the dispatcher"; return false; }
    uint32_t unwind_frame = 0;
    uint8_t prolog = 0;
    if (!FrameFromUnwindInfo(image + unwind_rva, size - unwind_rva, unwind_frame, &prolog)) {
        why = "dispatcher unwind info";
        return false;
    }
    if (acc_call_rva - begin_rva < prolog) { why = "accumulator call inside the prolog"; return false; }
    uint32_t prologue_frame = 0;
    const size_t avail = size - dispatcher_rva < 64 ? size - dispatcher_rva : 64;
    if (!FrameFromPrologue(image + dispatcher_rva, avail, prologue_frame)) { why = "dispatcher prologue frame"; return false; }
    if (prologue_frame != unwind_frame) { why = "prologue frame disagrees with the unwind info"; return false; }
    const uint32_t frame = unwind_frame + 8u;
    if (frame < kMinDispatcherFrame || frame > kMaxDispatcherFrame || (frame & 7u)) { why = "dispatcher frame size"; return false; }
    entry_frame = frame;
    return true;
}

// THE PUT-BACK SLOT: the last STICK call's write, put back over what a later extra call added. On Halo 4 and
// Reach the extra calls PRECEDE the stick call, so this is the previous frame's stick write, not the record's
// value at entry to the extra call. The two are the same unless something changed the record between frames
// (game code, a checkpoint revert, a seat entry or exit, an axis-ownership change, the setpoint stopping...),
// so the slot is honoured only while all of these still hold:
//   * no stick call has run since the write (the owner's stick sequence -- every stick call steps it);
//   * the same record (pc), and the publication it was written under has not changed seat context;
//   * the record has not moved further than an impulse could (max_delta: a teleport or a scripted facing is
//     never reverted);
//   * a safety cap on a MONOTONIC clock (the owner's unit, e.g. QPC counts): Halo 4 can run the impulse with
//     no stick call in the pass, and a pass with no stick at all must not hold an old write for long.
// Halo 3 / ODST (same_frame) differ: their extra calls run AFTER the stick call that wrote the slot (a kick in
// that frame's ticks), so the slot is this frame's write, and the filter is only the same record, inside the
// window -- with no context or plausibility test. Only the window is the owner's to choose per caller
// (check()'s `cap`).
struct Context {
    bool seated = false;
    uint32_t seat_key = 0;
    uint32_t axes = 0;          // the publication's seat axes (bit 0 yaw, bit 1 pitch)
    bool pitch_latch = true;
    bool operator==(const Context& o) const
    {
        return seated == o.seated && seat_key == o.seat_key && axes == o.axes && pitch_latch == o.pitch_latch;
    }
    bool operator!=(const Context& o) const { return !(*this == o); }
};

enum class Refusal : uint8_t { None, Invalid, Record, StickSince, Context, Age, Implausible };
constexpr int kRefusalCount = 7;

inline const char* RefusalName(Refusal r)
{
    switch (r) {
    case Refusal::None: return "none";
    case Refusal::Invalid: return "nothing written";
    case Refusal::Record: return "record";
    case Refusal::StickSince: return "stick since";
    case Refusal::Context: return "context";
    case Refusal::Age: return "age";
    default: return "implausible";
    }
}

struct PutBack {
    float max_delta = 0.1745f;  // 10 deg: an impulse, a sway or a kick moves the record less
    uint64_t max_age = 0;       // in the owner's clock units; 0 = no cap (the owner always sets one)
    bool same_frame = false;    // direct mode (Halo 3 / ODST): this frame's write; no context or delta test

    bool valid = false;
    uintptr_t pc = 0;
    bool axis[2]{};
    float value[2]{};
    uint64_t at = 0;
    uint32_t stick_seq = 0;
    Context ctx{};

    void invalidate() { valid = false; }

    void wrote(uintptr_t record, const bool axes[2], const float values[2], const Context& c, uint32_t seq,
               uint64_t now)
    {
        valid = record != 0 && (axes[0] || axes[1]) && (!axes[0] || std::isfinite(values[0])) &&
                (!axes[1] || std::isfinite(values[1]));
        pc = record;
        axis[0] = axes[0];
        axis[1] = axes[1];
        value[0] = values[0];
        value[1] = values[1];
        at = now;
        stick_seq = seq;
        ctx = c;
    }

    // Whether this extra call may put the slot back. `native`: the record after the extra call. Yaw wraps.
    Refusal check(uintptr_t record, const Context& c, uint32_t seq, uint64_t now, const float native[2]) const
    {
        return check(record, c, seq, now, native, max_age);
    }

    // The same under this call's own window `cap` (0 = none). Same-frame slots ignore `c` and `native`.
    Refusal check(uintptr_t record, const Context& c, uint32_t seq, uint64_t now, const float native[2],
                  uint64_t cap) const
    {
        if (!valid) return Refusal::Invalid;
        if (!record || record != pc) return Refusal::Record;
        if (seq != stick_seq) return Refusal::StickSince;
        if (!same_frame && c != ctx) return Refusal::Context;
        if (now < at || (cap && now - at > cap)) return Refusal::Age;
        if (same_frame) return Refusal::None;
        for (int a = 0; a < 2; ++a) {
            if (!axis[a]) continue;
            if (!std::isfinite(native[a])) return Refusal::Implausible;
            const float d = a == 0 ? std::remainder(native[a] - value[a], 6.28318531f) : native[a] - value[a];
            if (!(std::fabs(d) <= max_delta)) return Refusal::Implausible;
        }
        return Refusal::None;
    }
};

// Non-negative float maximum in an atomic (bit patterns of non-negative IEEE floats order like the floats).
inline void AtomicMaxFloat(std::atomic<uint32_t>& slot, float v)
{
    if (!(v >= 0.0f) || !std::isfinite(v)) return;
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    uint32_t cur = slot.load(std::memory_order_relaxed);
    while (bits > cur && !slot.compare_exchange_weak(cur, bits, std::memory_order_relaxed)) {}
}

inline float BitsToFloat(uint32_t bits)
{
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

inline void AtomicMaxU32(std::atomic<uint32_t>& slot, uint32_t v)
{
    uint32_t cur = slot.load(std::memory_order_relaxed);
    while (v > cur && !slot.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

// DEV CENSUS: calls per sim tick, per caller. The tick counter only advances inside the per-tick loop, so the
// calls that saw one tick value are one tick's worth of frames: stick calls per tick = frames per tick
// (2.0 at 120 fps over a 60 Hz sim). note() runs on the hook's (sim) thread; take() on the cold worker. The
// working state of the current tick is the sim thread's alone and is folded into the atomics on each tick
// change (`!=`: a checkpoint revert or a new map sends the tick back). A tick with more than kStalledPasses
// PASSES (a pass ends at its stick or no-unit call: one per frame), or more than kStalledCalls calls of any
// kind, is a pause or a load (the counter freezes while no tick runs): counted as stalled and kept out of the
// means. Passes, not calls: a 144 fps Halo 4 frame under fire runs impulse, sway and stick, so a tick of three
// frames is nine calls and no stall.
struct CensusTotals {
    uint32_t calls[2][kCallerCount]{};  // [seated][role], every call
    uint32_t ticks = 0;                 // folded ticks that were not stalled
    uint32_t sticks = 0;                // stick calls in those ticks
    uint32_t max_calls = 0;             // in one folded tick
    uint32_t extra_before_stick = 0;    // ticks in which an extra call ran ahead of a stick call (extras_lead)
    uint32_t extra_passes = 0;          // stick calls with an extra call ahead of them in the same pass (ditto)
    uint32_t no_stick = 0;              // ticks with no stick call
    uint32_t stalled = 0;
    uint32_t went_back = 0;
    uint32_t unreadable = 0;            // calls whose tick could not be read
};

struct TickCensus {
    static constexpr uint32_t kStalledPasses = 8;   // 8 frames in one tick: 480 fps over a 60 Hz sim
    static constexpr uint32_t kStalledCalls = 32;   // a stall that runs only extra calls (no pass ends)

    // Sim thread only. A PASS (one control update) ends at its stick or no-unit call, and the tick cannot change
    // inside a pass (the ticks run after it). `extras_lead`: the title's extra calls come FIRST in their pass
    // (dispatcher mode, measured: Halo 4's impulse and sway, Reach's impulse), so an extra call ahead of a stick
    // call is that pass's own. In direct mode (Halo 3 / ODST) the kick runs in a tick AFTER its stick call, so
    // it would be counted ahead of the next frame's stick: the order is not measured there (restart()).
    bool extras_lead = true;
    bool have = false;
    uint32_t tick = 0;
    uint32_t group_calls = 0, group_passes = 0, group_sticks = 0;
    bool group_extra_first = false;
    bool pass_extra = false;

    // Shared.
    std::atomic<uint32_t> calls[2][kCallerCount]{};
    std::atomic<uint32_t> ticks{0}, sticks{0}, max_calls{0}, extra_before_stick{0}, extra_passes{0}, no_stick{0};
    std::atomic<uint32_t> stalled{0}, went_back{0}, unreadable{0};

    void restart(bool extras_lead_in)
    {
        extras_lead = extras_lead_in;
        have = false;
        tick = 0;
        group_calls = group_passes = group_sticks = 0;
        group_extra_first = false;
        pass_extra = false;
    }

    void note(bool tick_ok, uint32_t tick_now, Caller c, bool seated)
    {
        calls[seated ? 1 : 0][static_cast<int>(c)].fetch_add(1, std::memory_order_relaxed);
        const bool extra = extras_lead && (c == Caller::Impulse || c == Caller::Sway || c == Caller::Kick);
        const bool stick = c == Caller::Stick;
        const bool pass_end = stick || c == Caller::NoUnit;
        if (stick && pass_extra) extra_passes.fetch_add(1, std::memory_order_relaxed);
        const bool extra_ahead = stick && pass_extra;
        if (extra) pass_extra = true;
        else if (pass_end) pass_extra = false;
        if (!tick_ok) {
            unreadable.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!have) {
            have = true;
            tick = tick_now;
        } else if (tick_now != tick) {
            fold();
            if (static_cast<int32_t>(tick_now - tick) < 0) went_back.fetch_add(1, std::memory_order_relaxed);
            tick = tick_now;
        }
        ++group_calls;
        if (pass_end) ++group_passes;
        if (stick) ++group_sticks;
        if (extra_ahead) group_extra_first = true;
    }

    CensusTotals take()
    {
        CensusTotals t;
        for (int s = 0; s < 2; ++s)
            for (int r = 0; r < kCallerCount; ++r) t.calls[s][r] = calls[s][r].exchange(0, std::memory_order_relaxed);
        t.ticks = ticks.exchange(0, std::memory_order_relaxed);
        t.sticks = sticks.exchange(0, std::memory_order_relaxed);
        t.max_calls = max_calls.exchange(0, std::memory_order_relaxed);
        t.extra_before_stick = extra_before_stick.exchange(0, std::memory_order_relaxed);
        t.extra_passes = extra_passes.exchange(0, std::memory_order_relaxed);
        t.no_stick = no_stick.exchange(0, std::memory_order_relaxed);
        t.stalled = stalled.exchange(0, std::memory_order_relaxed);
        t.went_back = went_back.exchange(0, std::memory_order_relaxed);
        t.unreadable = unreadable.exchange(0, std::memory_order_relaxed);
        return t;
    }

private:
    void fold()
    {
        if (group_passes > kStalledPasses || group_calls > kStalledCalls) {
            stalled.fetch_add(1, std::memory_order_relaxed);
        } else if (group_calls) {
            ticks.fetch_add(1, std::memory_order_relaxed);
            sticks.fetch_add(group_sticks, std::memory_order_relaxed);
            AtomicMaxU32(max_calls, group_calls);
            if (group_extra_first) extra_before_stick.fetch_add(1, std::memory_order_relaxed);
            if (!group_sticks) no_stick.fetch_add(1, std::memory_order_relaxed);
        }
        group_calls = group_passes = group_sticks = 0;
        group_extra_first = false;
    }
};

} // namespace aim_caller
