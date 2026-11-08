// Seat frame readers (src/common/seat_frame_sites.h, src/dll/seat_frame_reader.h): SeatFrameRead over a synthetic
// world for every title -- level, tilted, bit 4, no seat, an attached vehicle through its carrier's node (with the
// sign extensions), segmented and negative tag addresses, Halo 4's parent type mask -- the case decision with and
// without our skip, and the live Bind over an executable fixture holding each title's gate and getter at their
// RVAs (bound; refused on a moved byte, a foreign jne, a call that misses the getter; our E9 adopted as "skip").
//
// The same world, laid out from the same table fields, is what tools/verify_seat_frame_conversion.py runs the
// RETAIL rebase over; this file proves the C++ reader reads it as the game's code does there.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../src/common/seat_frame_sites.h"
#include "../src/dll/seat_frame_reader.h"

namespace
{
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

using namespace seat_frame;
constexpr float kDeg = 0.017453292519943295f;

// ---- a synthetic world ------------------------------------------------------------------------------------------
struct Arena
{
    std::vector<uint8_t> bytes;
    uintptr_t next = 0x100;
    explicit Arena(size_t n) : bytes(n, 0xDD) {}
    uintptr_t base() const { return reinterpret_cast<uintptr_t>(bytes.data()); }
    uintptr_t Alloc(size_t n)
    {
        const uintptr_t at = base() + ((next + 0x3F) & ~uintptr_t{0x3F});
        next = (next + 0x3F & ~uintptr_t{0x3F}) + n + 0x100;
        return at;
    }
    bool In(uintptr_t at, size_t n) const { return at >= base() && at + n <= base() + bytes.size(); }
    template <class T>
    void Put(uintptr_t at, T v) { memcpy(reinterpret_cast<void*>(at), &v, sizeof(T)); }
};

struct FakeMem
{
    Arena* arena;
    std::map<uint32_t, std::pair<uintptr_t, uint32_t>> objects;   // handle -> data, type
    template <class T>
    bool R(uintptr_t at, T& out)
    {
        if (!arena->In(at, sizeof(T)))
            return false;
        memcpy(&out, reinterpret_cast<const void*>(at), sizeof(T));
        return true;
    }
    bool U8(uintptr_t at, uint8_t& o) { return R(at, o); }
    bool U16(uintptr_t at, uint16_t& o) { return R(at, o); }
    bool U32(uintptr_t at, uint32_t& o) { return R(at, o); }
    bool U64(uintptr_t at, uint64_t& o) { return R(at, o); }
    bool F32(uintptr_t at, float& o) { return R(at, o); }
    uintptr_t Object(uint32_t handle, uint32_t mask)
    {
        const auto it = objects.find(handle);
        if (it == objects.end())
            return 0;
        if (mask && !((mask >> it->second.second) & 1u))
            return 0;
        return it->second.first;
    }
};

struct Knobs
{
    float pitch = 0.0f, roll = 0.0f;
    int seat = 1;
    uint32_t flags = 0x21;
    bool attached = false;
    uint8_t node = 3;
    uint16_t table = 0x400;
    float nodeRoll = 0.0f;
    bool alt = false;      // segment 3/5, or Halo 2's negative block address
    bool badType = false;  // Halo 4: the parent is not one of 0x2003's types
    bool noGate = false;
};

void Rot(float pitch, float roll, const float in[3], float out[3])
{
    // about Y by pitch, then about X by roll
    const float x = in[0] * std::cos(pitch) + in[2] * std::sin(pitch);
    const float z0 = -in[0] * std::sin(pitch) + in[2] * std::cos(pitch);
    const float y = in[1] * std::cos(roll) - z0 * std::sin(roll);
    const float z = in[1] * std::sin(roll) + z0 * std::cos(roll);
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

struct World
{
    Arena arena{0x400000};
    FakeMem mem{&arena, {}};
    ReaderGlobals g{};
    uint32_t unit = 0;
    float up[3]{}, fwd[3]{};   // the frame the reader should report (composed when attached)

    uintptr_t Object(uint32_t index, uint16_t salt, uint32_t type, uint32_t& handle)
    {
        const uintptr_t data = arena.Alloc(0x40000) + 0x20000;   // room for a negative node table offset
        handle = (uint32_t{salt} << 16) | index;
        mem.objects[handle] = {data, type};
        return data;
    }

    World(const ReaderSite& s, const Knobs& k)
    {
        // Globals live in the arena too (they are module .data in the game).
        g.tagInstances = arena.Alloc(8);
        g.tagBase = arena.Alloc(16 * 8);
        g.splitNegative = arena.Alloc(8);
        g.splitPositive = arena.Alloc(8);
        uint32_t vehicle = 0;
        const uintptr_t u = Object(3, 0x1234, 0, unit);
        const uintptr_t v = Object(7, 0x2222, k.badType ? 2u : 1u, vehicle);
        arena.Put<uint32_t>(u + s.unitGate, k.noGate ? kNone : 0x12345678u);
        arena.Put<int16_t>(u + s.unitSeat, static_cast<int16_t>(k.seat));
        arena.Put<uint32_t>(u + s.objectParent, vehicle);
        const float x[3]{1, 0, 0}, z[3]{0, 0, 1};
        float lf[3], lu[3];
        Rot(k.pitch * kDeg, k.roll * kDeg, x, lf);
        Rot(k.pitch * kDeg, k.roll * kDeg, z, lu);
        for (int i = 0; i < 3; ++i)
        {
            arena.Put<float>(v + s.localForward + 4 * i, lf[i]);
            arena.Put<float>(v + s.localUp + 4 * i, lu[i]);
        }
        memcpy(fwd, lf, sizeof(fwd));
        memcpy(up, lu, sizeof(up));
        if (k.attached)
        {
            uint32_t carrier = 0;
            const uintptr_t c = Object(9, 0x3333, 1, carrier);
            arena.Put<uint32_t>(v + s.objectParent, carrier);
            arena.Put<uint8_t>(v + s.nodeIndex, k.node);
            arena.Put<uint16_t>(c + s.nodeTable, k.table);
            const int64_t node = s.nodeIndexSigned ? static_cast<int8_t>(k.node) : k.node;
            const int64_t table = s.nodeTableSigned ? static_cast<int16_t>(k.table) : k.table;
            const uintptr_t m = static_cast<uintptr_t>(c + table + node * 0x34);
            const float cs = std::cos(k.nodeRoll * kDeg), sn = std::sin(k.nodeRoll * kDeg);
            const float cols[3][3]{{1, 0, 0}, {0, cs, sn}, {0, -sn, cs}};   // rotation about X
            arena.Put<float>(m, 1.0f);
            for (int c3 = 0; c3 < 3; ++c3)
                for (int i = 0; i < 3; ++i)
                    arena.Put<float>(m + (c3 == 0 ? 4 : c3 == 1 ? 0x10 : 0x1C) + 4 * i, cols[c3][i]);
            for (int i = 0; i < 3; ++i)
            {
                fwd[i] = lf[0] * cols[0][i] + lf[1] * cols[1][i] + lf[2] * cols[2][i];
                up[i] = lu[0] * cols[0][i] + lu[1] * cols[1][i] + lu[2] * cols[2][i];
            }
        }
        else
            arena.Put<uint32_t>(v + s.objectParent, kNone);
        constexpr uint16_t definition = 0x42;
        arena.Put<uint16_t>(v + s.definitionIndex, definition);
        const uintptr_t instances = arena.Alloc(0x1000);
        arena.Put<uint64_t>(g.tagInstances, instances);
        const int seat = k.seat < 0 ? 0 : k.seat;
        if (s.scheme == kTagLinear)
        {
            const uintptr_t base = arena.Alloc(0x40000);
            arena.Put<uint64_t>(g.tagBase, base);
            arena.Put<uint32_t>(instances + definition * 8 + 4, 0x100u);
            arena.Put<uint32_t>(base + 0x100 * 4 + s.seatBlock, 0x2000u);
            arena.Put<uint32_t>(base + 0x2000 * 4 + seat * s.seatStride, k.flags);
        }
        else if (s.scheme == kTagSegmented)
        {
            const uint32_t sd = k.alt ? 3 : 0, sb = k.alt ? 5 : 0;
            std::map<uint32_t, uintptr_t> regions;
            for (uint32_t seg : {sd, sb})
                if (!regions.count(seg))
                {
                    regions[seg] = arena.Alloc(0x10000);
                    arena.Put<uint64_t>(g.tagBase + seg * 8, regions[seg] - (uint64_t{seg} << 28) * 4);
                }
            arena.Put<uint32_t>(instances + definition * 8 + 4, (sd << 28) | 0x1000);
            arena.Put<uint32_t>(regions[sd] + 0x1000 * 4 + s.seatBlock, (sb << 28) | 0x2000);
            arena.Put<uint32_t>(regions[sb] + 0x2000 * 4 + seat * s.seatStride, k.flags);
        }
        else
        {
            const uintptr_t base = arena.Alloc(0x4000), neg = arena.Alloc(0x4000), pos = arena.Alloc(0x4000);
            arena.Put<uint64_t>(g.tagBase, base);
            arena.Put<uint64_t>(g.splitNegative, neg);
            arena.Put<uint64_t>(g.splitPositive, pos);
            arena.Put<int32_t>(instances + definition * 16 + 8, 0x800);
            arena.Put<uint32_t>(base + 0x800 + s.seatBlock, k.alt ? 0x80000300u : 0x300u);
            arena.Put<uint32_t>((k.alt ? neg : pos) + 0x300 + seat * s.seatStride, k.flags);
        }
    }

    ReadResult Read(const ReaderSite& s) { return SeatFrameRead(s, g, mem, unit); }
};

bool Near3(const float a[3], const float b[3], float t = 1e-5f)
{
    return std::fabs(a[0] - b[0]) <= t && std::fabs(a[1] - b[1]) <= t && std::fabs(a[2] - b[2]) <= t;
}

void TestSite(const ReaderSite& s)
{
    char what[192];
    const auto label = [&](const char* text) {
        snprintf(what, sizeof(what), "%s: %s", s.name, text);
        return what;
    };
    Check(seat_frame_rt::PatternBytes(s.gatePattern) == s.jccRva - s.gateRva,
          label("the gate pattern ends at the jne"));
    {
        World w(s, Knobs{});
        const ReadResult r = w.Read(s);
        Frame f{};
        Check(r.readable && r.gateSeated && r.flagsKnown && r.flags == 0x21 && r.upKnown && Near3(r.up, w.up),
              label("level: flags and up read"));
        Check(Decide(r, false, &f) == Case::Rebased, label("level, no skip: case b (R is the identity)"));
        Check(Decide(r, true, nullptr) == Case::SkipWorld, label("level, our skip in place: case a"));
    }
    {
        Knobs k;
        k.pitch = 30.0f;
        k.roll = -12.0f;
        k.seat = 2;
        World w(s, k);
        const ReadResult r = w.Read(s);
        Frame f{};
        Check(r.flagsKnown && r.flags == 0x21 && Near3(r.up, w.up) && Decide(r, false, &f) == Case::Rebased &&
                  Near3(f.u, w.up),
              label("tilted, seat 2: the stride, and R built on the read up"));
    }
    {
        Knobs k;
        k.pitch = 30.0f;
        k.flags = 0x31;
        World w(s, k);
        const ReadResult r = w.Read(s);
        Check(r.flagsKnown && (r.flags & 0x10) && !r.upKnown && Decide(r, false, nullptr) == Case::World,
              label("bit 4: world, and no up is read (the rebase exits first)"));
    }
    {
        Knobs k;
        k.seat = -1;
        World w(s, k);
        const ReadResult r = w.Read(s);
        Check(r.readable && !r.gateSeated && Decide(r, false, nullptr) == Case::World, label("no seat: world"));
    }
    {
        Knobs k;
        k.noGate = true;
        World w(s, k);
        Check(Decide(w.Read(s), false, nullptr) == Case::World, label("no gate field: world"));
    }
    {
        Knobs k;
        k.attached = true;
        k.pitch = -8.0f;
        k.nodeRoll = 28.0f;
        k.node = 0x90;
        k.table = 0xFC00;
        World w(s, k);
        const ReadResult r = w.Read(s);
        Check(r.upKnown && Near3(r.up, w.up) && Near3(r.forward, w.fwd) && Decide(r, false, nullptr) == Case::Rebased,
              label("attached, node 0x90, table 0xFC00: up composed through the carrier's node"));
    }
    {
        Knobs k;
        k.alt = true;
        k.pitch = -15.0f;
        World w(s, k);
        const ReadResult r = w.Read(s);
        Check(r.flagsKnown && r.flags == 0x21, label("segment / negative-address variant: flags read"));
    }
    if (s.parentTypeMask)
    {
        Knobs k;
        k.badType = true;
        World w(s, k);
        const ReadResult r = w.Read(s);
        Check(r.readable && !r.gateSeated && Decide(r, false, nullptr) == Case::World,
              label("parent outside the type mask: the typed lookup's early exit, world"));
    }
    {
        // A wrong salt is a broken link: unknown, never world.
        World w(s, Knobs{});
        const ReadResult r = SeatFrameRead(s, w.g, w.mem, w.unit ^ 0x10000u);
        Check(!r.readable && Decide(r, false, nullptr) == Case::Unknown, label("stale unit handle: unknown"));
    }
    {
        // A scaled node (a non-unit up) fails the value check: unknown, no write.
        Knobs k;
        k.attached = true;
        World w(s, k);
        const ReadResult good = w.Read(s);
        (void)good;
        // Scale the node's up column by 1.1.
        uintptr_t c = 0;
        for (const auto& [h, o] : w.mem.objects)
            if ((h & 0xFFFF) == 9)
                c = o.first;
        const int64_t node = s.nodeIndexSigned ? static_cast<int8_t>(k.node) : k.node;
        const int64_t table = s.nodeTableSigned ? static_cast<int16_t>(k.table) : k.table;
        const uintptr_t m = static_cast<uintptr_t>(c + table + node * 0x34);
        w.arena.Put<float>(m + 0x1C + 8, 1.1f);
        const ReadResult r = w.Read(s);
        Check(r.flagsKnown && !r.upKnown && Decide(r, false, nullptr) == Case::Unknown,
              label("a scaled up fails the value check: unknown (no seated write)"));
        Check(Decide(r, true, nullptr) == Case::SkipWorld, label("...but with our skip the up is not needed: case a"));
    }
}

// ---- Bind over an executable fixture -----------------------------------------------------------------------------
std::vector<uint8_t> Bytes(const char* pattern, uint8_t fill)
{
    std::vector<uint8_t> out;
    for (const char* p = pattern; *p;)
    {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;
        if (p[0] == '?')
            out.push_back(fill);
        else
            out.push_back(static_cast<uint8_t>(strtoul(std::string(p, 2).c_str(), nullptr, 16)));
        p += 2;
    }
    return out;
}

void TestBind(const ReaderSite& s)
{
    const size_t size = (std::max)(s.gateRva, s.getterRva) + 0x400000;
    auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    char what[160];
    const auto label = [&](const char* text) {
        snprintf(what, sizeof(what), "%s bind: %s", s.name, text);
        return what;
    };
    const auto build = [&] {
        memset(base, 0xCC, size);
        // Wildcard bytes as zero: every rip displacement lands just past its instruction, inside the module.
        const auto gate = Bytes(s.gatePattern, 0x00), getter = Bytes(s.getterPattern, 0x00);
        memcpy(base + s.gateRva, gate.data(), gate.size());
        memcpy(base + s.getterRva, getter.data(), getter.size());
        base[s.jccRva] = 0x0F;
        base[s.jccRva + 1] = 0x85;
        base[s.getterCallRva] = 0xE8;
        const int32_t rel = static_cast<int32_t>(static_cast<int64_t>(s.getterRva) - (s.getterCallRva + 5));
        memcpy(base + s.getterCallRva + 1, &rel, 4);
        // The getter's own TLS index (Halo 2: objects) load must name the gate's global: aim both at one address.
        const uint32_t gateLoad = s.scheme == kTagSplit ? s.objectsLoad : s.tlsIndexLoad;
        const uint32_t getterLoad = s.getterRva + (s.scheme == kTagSplit ? 0x14 : 0x04);
        const uint32_t target = s.gateRva + gateLoad + 7;   // where the gate's zero disp points
        const int32_t disp = static_cast<int32_t>(static_cast<int64_t>(target) - (getterLoad + 7));
        memcpy(base + getterLoad + 3, &disp, 4);
    };
    seat_frame_rt::Binding b;
    const char* why = nullptr;
    build();
    Check(seat_frame_rt::Bind(b, s, reinterpret_cast<uintptr_t>(base), size, why) && b.ready && !why,
          label("the fixture binds"));
    Check(!seat_frame_rt::SkipPatched(b), label("stock jne: no skip"));
    base[s.jccRva] = 0xE9;
    Check(seat_frame_rt::SkipPatched(b), label("our E9 reads as the skip"));
    Check(seat_frame_rt::Bind(b, s, reinterpret_cast<uintptr_t>(base), size, why), label("binds over our E9 too"));
    build();
    base[s.jccRva] = 0x74;
    Check(!seat_frame_rt::Bind(b, s, reinterpret_cast<uintptr_t>(base), size, why) && !b.ready,
          label("refuses a foreign instruction at the jne"));
    build();
    base[s.gateRva + 12] ^= 0xFF;
    Check(!seat_frame_rt::Bind(b, s, reinterpret_cast<uintptr_t>(base), size, why), label("refuses a moved gate byte"));
    build();
    base[s.getterCallRva + 1] ^= 0x10;
    Check(!seat_frame_rt::Bind(b, s, reinterpret_cast<uintptr_t>(base), size, why),
          label("refuses a call that misses the getter"));
    build();
    base[s.getterRva + (s.scheme == kTagSplit ? 0x17 : 0x07)] ^= 0x40;   // the getter's global disp
    Check(!seat_frame_rt::Bind(b, s, reinterpret_cast<uintptr_t>(base), size, why),
          label("refuses a getter whose TLS index / objects global is not the gate's"));
    VirtualFree(base, 0, MEM_RELEASE);
}
} // namespace

int main()
{
    for (const ReaderSite* s : {&kHalo3Reader, &kOdstReader, &kReachReader, &kHalo2Reader, &kHalo4Reader})
    {
        TestSite(*s);
        TestBind(*s);
    }
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
