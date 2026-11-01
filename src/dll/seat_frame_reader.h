#pragma once
// SEAT FRAME READER, live (src/common/seat_frame_sites.h has the table and the read; seat_frame_logic.h the math).
// Included by game.cpp (Halo 3, ODST, Reach, Halo 4) and halo2_observer_6dof.cpp (Halo 2).
//
// Each title's direct drive binds one of these when its control-update hook installs, and reads it on the
// SIMULATION thread inside that hook, in the same update it writes the record: what the title's rebase will do to
// the record this tick (world, our skip, or R(u)), with u read the way the rebase reads it. Two clocks: nothing
// here is sampled on another thread or kept across updates.
//
// Bind (the drive's install thread): both patterns unique at their RVAs, the getter call landing on the getter,
// every decoded global inside the module, the getter's TLS index (Halo 2: objects array) the gate's, and the
// bytes at the bit-4 test either the stock `0F 85` or our own `E9` (seat_aim_world_frame). Anything else: not
// bound, and the title's seated writes stand down (the servo keeps the seat), said once by the caller.
// Read: SEH-guarded raw reads; objects resolved salt-checked (TLS titles through the gate's own TLS index and
// object table, Halo 2 through the caller's proven accessor). No allocation, no lock, no log.

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "../common/seat_frame_sites.h"
#include "sigscan.h"

namespace seat_frame_rt
{
struct Binding
{
    const seat_frame::ReaderSite* site = nullptr;
    uintptr_t base = 0;
    size_t size = 0;
    seat_frame::ReaderGlobals globals{};
    uintptr_t objectsArray = 0;    // Halo 2: the objects array pointer variable the gate loads
    const uint8_t* jcc = nullptr;  // the bit-4 jne (E9 = our skip)
    std::atomic<bool> ready{false};
};

inline bool UniqueAt(uintptr_t base, size_t size, uint32_t rva, const char* pattern)
{
    const uintptr_t first = sig::Find(base, size, pattern);
    if (first != base + rva)
        return false;
    const uintptr_t next = first + 1;
    return next >= base + size || !sig::Find(next, static_cast<size_t>(base + size - next), pattern);
}

inline size_t PatternBytes(const char* pattern)
{
    size_t n = 0;
    for (const char* p = pattern; *p; ++p)
        if (*p != ' ' && (p == pattern || p[-1] == ' '))
            ++n;
    return n;
}

// A 7-byte rip-relative instruction at `at` (disp32 at +3): its target, or 0 when outside the module.
inline uintptr_t RipGlobal(uintptr_t base, size_t size, uintptr_t at)
{
    int32_t disp = 0;
    memcpy(&disp, reinterpret_cast<const void*>(at + 3), sizeof(disp));
    const uintptr_t target = at + 7 + static_cast<intptr_t>(disp);
    return target >= base && target + 8 <= base + size ? target : 0;
}

// Install-thread: prove and bind. `why` names the first proof that failed (nullptr when bound).
inline bool Bind(Binding& b, const seat_frame::ReaderSite& site, uintptr_t base, size_t size, const char*& why)
{
    b.ready.store(false, std::memory_order_release);
    why = nullptr;
    const size_t gateBytes = PatternBytes(site.gatePattern);
    if (!base || site.gateRva + gateBytes + 6 > size || site.getterRva + PatternBytes(site.getterPattern) > size)
        why = "the module is too small";
    else if (site.gateRva + gateBytes != site.jccRva)
        why = "the table's gate does not end at its jne";
    else if (!UniqueAt(base, size, site.gateRva, site.gatePattern))
        why = "the rebase's gate pattern is missing, ambiguous or moved";
    else if (!UniqueAt(base, size, site.getterRva, site.getterPattern))
        why = "the up getter's pattern is missing, ambiguous or moved";
    if (why)
        return false;
    const auto* call = reinterpret_cast<const uint8_t*>(base + site.getterCallRva);
    int32_t rel = 0;
    memcpy(&rel, call + 1, sizeof(rel));
    if (call[0] != 0xE8 || site.getterCallRva + 5 + static_cast<int64_t>(rel) != site.getterRva)
    {
        why = "the rebase's call does not land on the up getter";
        return false;
    }
    const auto* jcc = reinterpret_cast<const uint8_t*>(base + site.jccRva);
    if (!((jcc[0] == 0x0F && jcc[1] == 0x85) || jcc[0] == 0xE9))
    {
        why = "the bytes at the bit-4 test are neither the stock jne nor our skip";
        return false;
    }
    const uintptr_t gate = base + site.gateRva;
    const auto global = [&](uint32_t offset) -> uintptr_t {
        return offset == seat_frame::kNone ? 0 : RipGlobal(base, size, gate + offset);
    };
    seat_frame::ReaderGlobals g{};
    g.tlsIndex = global(site.tlsIndexLoad);
    g.tagInstances = global(site.tagInstancesLoad);
    g.tagBase = global(site.tagBaseLoad);
    g.splitNegative = global(site.splitNegativeLoad);
    g.splitPositive = global(site.splitPositiveLoad);
    const uintptr_t objects = global(site.objectsLoad);
    const bool tlsTitle = site.scheme != seat_frame::kTagSplit;
    // The getter loads its TLS index (Halo 2: the objects array) itself; it must be the gate's.
    const uintptr_t getterGlobal = RipGlobal(base, size, base + site.getterRva + (tlsTitle ? 0x04 : 0x14));
    if (!g.tagInstances || !g.tagBase || (tlsTitle && !g.tlsIndex) ||
        (!tlsTitle && (!objects || !g.splitNegative || !g.splitPositive)))
    {
        why = "a global the rebase loads lies outside the module";
        return false;
    }
    if (getterGlobal != (tlsTitle ? g.tlsIndex : objects))
    {
        why = tlsTitle ? "the getter's engine TLS index is not the rebase's"
                       : "the getter's objects array is not the rebase's";
        return false;
    }
    b.site = &site;
    b.base = base;
    b.size = size;
    b.globals = g;
    b.objectsArray = objects;
    b.jcc = jcc;
    b.ready.store(true, std::memory_order_release);
    return true;
}

inline void Unbind(Binding& b)
{
    b.ready.store(false, std::memory_order_release);
}

// Raw memory for SeatFrameRead. Every read may fault: the caller wraps the whole read in SEH.
struct RawMem
{
    const Binding* b = nullptr;
    // Halo 2: the proven salt-checked accessor (Halo2ObjectFromIndex); null for the TLS titles.
    void* (*halo2Object)(uint32_t) = nullptr;

    template <class T>
    bool Read(uintptr_t at, T& out) const
    {
        if (at < 0x10000)
            return false;
        memcpy(&out, reinterpret_cast<const void*>(at), sizeof(T));
        return true;
    }
    bool U8(uintptr_t at, uint8_t& out) const { return Read(at, out); }
    bool U16(uintptr_t at, uint16_t& out) const { return Read(at, out); }
    bool U32(uintptr_t at, uint32_t& out) const { return Read(at, out); }
    bool U64(uintptr_t at, uint64_t& out) const { return Read(at, out); }
    bool F32(uintptr_t at, float& out) const { return Read(at, out) && std::isfinite(out); }

    // The object's data. TLS titles: TLS[index] -> table (+objectTls) -> entries (+objectEntries) -> entry
    // (0x18 each: salt word +0, type byte +4, data +0x10), exactly the gate's walk, salt-checked. A typed request
    // (Halo 4's parent: 0x5DA400's mask) also requires the type bit.
    uintptr_t Object(uint32_t handle, uint32_t typeMask) const
    {
        if (handle == seat_frame::kNone || !(handle >> 16))
            return 0;
        if (halo2Object)
            return reinterpret_cast<uintptr_t>(halo2Object(handle));
        const seat_frame::ReaderSite& site = *b->site;
        uint32_t index = 0;
        if (!U32(b->globals.tlsIndex, index) || index >= 1088)
            return 0;
        auto** slots = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
        const uint8_t* tls = slots ? slots[index] : nullptr;
        uint64_t table = 0, entries = 0, data = 0;
        if (!tls || !U64(reinterpret_cast<uintptr_t>(tls) + site.objectTls, table) || !table ||
            !U64(table + site.objectEntries, entries) || !entries)
            return 0;
        const uintptr_t entry =
            static_cast<uintptr_t>(entries + (handle & 0xFFFFu) * uint64_t{seat_frame::kObjectEntryStride});
        uint16_t salt = 0;
        uint8_t type = 0;
        if (!U16(entry, salt) || salt != static_cast<uint16_t>(handle >> 16) || !U8(entry + 4, type) ||
            !U64(entry + seat_frame::kObjectEntryData, data) || !data)
            return 0;
        if (typeMask && (type >= 32 || !((typeMask >> type) & 1u)))
            return 0;
        return static_cast<uintptr_t>(data);
    }
};

// Whether our skip jump is in the rebase's bit-4 test right now. Code bytes: the WaitThread rewrites them only
// with every other thread frozen (seat_aim_world_frame.inl), so a read here sees one instruction or the other.
inline bool SkipPatched(const Binding& b)
{
    return b.jcc && b.jcc[0] == 0xE9;
}

// The result of one live read (sim thread).
struct Live
{
    seat_frame::Case kase = seat_frame::Case::Unknown;
    seat_frame::Frame frame{};       // R (identity unless kase == Rebased)
    float tiltDeg = std::nanf("");   // the parent's tilt when its up read (any case), else NaN
    bool seated = false;             // the rebase's own early exits pass: the unit has a parent and a seat
    // The occupied seat's LIVE flags word, as the rebase itself addresses it (its bit 4 is what the rebase tests;
    // the same word carries the seat's role: seat_converge_logic.h, RoleFromSeatFlags).
    bool flagsKnown = false;
    uint32_t flags = 0;
};

inline bool ReadGuarded(const Binding& b, const RawMem& mem, uint32_t unit, seat_frame::ReadResult& out)
{
    __try
    {
        RawMem m = mem;
        out = seat_frame::SeatFrameRead(*b.site, b.globals, m, unit);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Sim thread, inside the title's control-update hook: what the rebase will do to this unit's record this tick.
inline Live Read(const Binding& b, void* (*halo2Object)(uint32_t), uint32_t unit)
{
    Live live{};
    if (!b.ready.load(std::memory_order_acquire) || unit == seat_frame::kNone)
        return live;
    RawMem mem{&b, halo2Object};
    seat_frame::ReadResult read{};
    if (!ReadGuarded(b, mem, unit, read))
        return live;
    bool skip = false;
    __try
    {
        skip = SkipPatched(b);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return live;
    }
    live.kase = seat_frame::Decide(read, skip, &live.frame);
    live.seated = read.gateSeated;
    live.flagsKnown = read.flagsKnown;
    live.flags = read.flags;
    if (read.upKnown)
        live.tiltDeg = seat_frame::TiltDeg(read.up);
    return live;
}

// Sim thread: the forward and up (world) of the vehicle `unit` sits in, for any seat. False: not bound, not
// seated, or a link or a check failed.
inline bool ReadVehicleFrame(const Binding& b, void* (*halo2Object)(uint32_t), uint32_t unit, float forward[3],
                             float up[3])
{
    if (!b.ready.load(std::memory_order_acquire) || unit == seat_frame::kNone)
        return false;
    __try
    {
        RawMem mem{&b, halo2Object};
        return seat_frame::SeatedVehicleFrameRead(*b.site, mem, unit, forward, up);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Sim thread: the record angles to write for WORLD angles (yaw, pitch) through this update's case, and the game's
// own WORLD aim expressed in the record's frame (so a check compares like with like). World / SkipWorld: the
// identity. False only for Unknown, or a conversion that did not resolve (no seated write then).
inline bool ToRecord(const Live& live, float& yaw, float& pitch, float fallbackYaw)
{
    if (live.kase == seat_frame::Case::Unknown)
        return false;
    if (live.kase != seat_frame::Case::Rebased)
        return true;
    seat_frame::Angles a{};
    if (!seat_frame::RecordFromWorld(live.frame, yaw, pitch, fallbackYaw, a))
        return false;
    yaw = a.yaw;
    pitch = a.pitch;
    return true;
}

// Per title, sim thread writes / cold worker reads: how many seated updates read each case, and (dev) the frame
// check's worst sample, with the tilt and case at it.
struct Counters
{
    std::atomic<uint32_t> byCase[4]{};
    std::atomic<uint32_t> unknownLogged{0};   // the "frame not readable" line, logged once per binding
#if HALOMCCVR_DEV_DIAGNOSTICS
    std::atomic<uint64_t> worst{0};           // (error float bits << 32) | (tilt float bits); case in `worstCase`
    std::atomic<uint32_t> worstCase{0};
    std::atomic<uint32_t> samples{0};
    std::atomic<uint32_t> maxTiltBits{0};
#endif
};

inline void Count(Counters& c, seat_frame::Case kase)
{
    c.byCase[static_cast<int>(kase) & 3].fetch_add(1, std::memory_order_relaxed);
}

#if HALOMCCVR_DEV_DIAGNOSTICS
inline uint32_t FloatBits(float v)
{
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    return bits;
}

inline float BitsFloat(uint32_t bits)
{
    float v = 0.0f;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

// The frame check, per seated update: |R e(record) - e(game aim)| (the record and the game's world aim as read
// this update; R the identity unless the rebase runs).
inline void NoteFrameCheck(Counters& c, const Live& live, float recordYaw, float recordPitch, float aimYaw,
                           float aimPitch)
{
    if (live.kase == seat_frame::Case::Unknown)
        return;
    const float err = seat_frame::FrameCheckDeg(live.frame, recordYaw, recordPitch, aimYaw, aimPitch);
    if (!std::isfinite(err))
        return;
    c.samples.fetch_add(1, std::memory_order_relaxed);
    if (std::isfinite(live.tiltDeg))
    {
        uint32_t seen = c.maxTiltBits.load(std::memory_order_relaxed);
        while ((!seen || BitsFloat(seen) < live.tiltDeg) &&
               !c.maxTiltBits.compare_exchange_weak(seen, FloatBits(live.tiltDeg), std::memory_order_relaxed))
        {
        }
    }
    const uint64_t pair = static_cast<uint64_t>(FloatBits(err)) << 32 | FloatBits(live.tiltDeg);
    uint64_t seen = c.worst.load(std::memory_order_relaxed);
    while ((!seen || BitsFloat(static_cast<uint32_t>(seen >> 32)) < err))
    {
        if (c.worst.compare_exchange_weak(seen, pair, std::memory_order_relaxed))
        {
            c.worstCase.store(static_cast<uint32_t>(live.kase), std::memory_order_relaxed);
            break;
        }
    }
}

// Cold worker: the dev frame-check text for a seats line ("" when no seated update ran). `out` >= 160 chars.
inline void FrameCheckText(Counters& c, char* out, size_t n)
{
    out[0] = 0;
    const uint32_t samples = c.samples.exchange(0, std::memory_order_relaxed);
    const uint64_t pair = c.worst.exchange(0, std::memory_order_relaxed);
    const uint32_t kase = c.worstCase.exchange(0, std::memory_order_relaxed);
    const uint32_t maxTilt = c.maxTiltBits.exchange(0, std::memory_order_relaxed);
    if (!samples)
        return;
    const float err = BitsFloat(static_cast<uint32_t>(pair >> 32));
    const float tilt = BitsFloat(static_cast<uint32_t>(pair));
    char tiltText[24], maxText[24];
    if (std::isfinite(tilt))
        snprintf(tiltText, sizeof(tiltText), "%.1f deg", tilt);
    else
        snprintf(tiltText, sizeof(tiltText), "n/a");
    if (maxTilt && std::isfinite(BitsFloat(maxTilt)))
        snprintf(maxText, sizeof(maxText), "%.1f deg", BitsFloat(maxTilt));
    else
        snprintf(maxText, sizeof(maxText), "n/a");
    snprintf(out, n, "frame check: max |R*e(record) - game aim| %.2f deg at tilt %s (case %s; %u seated updates, "
             "max tilt %s)", err, tiltText, seat_frame::CaseName(static_cast<seat_frame::Case>(kase & 3)), samples,
             maxText);
}
#endif

// Cold worker: "frame world N, a N, b N, unread N" for a seats line ("" when no seated update read a frame).
inline bool CaseText(Counters& c, char* out, size_t n)
{
    uint32_t v[4];
    for (int i = 0; i < 4; ++i)
        v[i] = c.byCase[i].exchange(0, std::memory_order_relaxed);
    out[0] = 0;
    if (!v[0] && !v[1] && !v[2] && !v[3])
        return false;
    snprintf(out, n, "frame world %u, a %u, b %u, unread %u", v[static_cast<int>(seat_frame::Case::World)],
             v[static_cast<int>(seat_frame::Case::SkipWorld)], v[static_cast<int>(seat_frame::Case::Rebased)],
             v[static_cast<int>(seat_frame::Case::Unknown)]);
    return v[static_cast<int>(seat_frame::Case::Unknown)] != 0;
}
} // namespace seat_frame_rt
