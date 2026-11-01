#pragma once

// TYPE-13 FLIGHT CONTROLLERS, LOG-ONLY STAGE (dev builds only): the engine-free half of the observer on
// Reach's Sabre/Seraph pilot controller (haloreach+0x64828) and Halo 4's Broadsword pilot controller
// (halo4+0xAA524). Both dispatchers call these instead of the desired-angles accumulator the direct-drive hook
// sits on, so a type-13 pilot is never driven; docs/TYPE13-FLIGHT-CONTROLLERS-2026-10-01.md section 4 Phase B
// lists the questions only the running game can answer before a write is designed. This header holds what
// does not touch the game: the per-tick bookkeeping (one controller call or several per game tick), the
// stint (one vehicle flown without a break), the forced-steer window test, the turn-rate window, the caller
// frame derived from the dispatcher's own prologue, the owner-thread claim, and the lock-free ring the
// sim-thread hook hands its observations to the cold worker through (src/dll/type13_flight_log.inl).
//
// Writes nothing to the game, ever. Zero engine dependencies; unit-tested in tests/type13_flight_log_tests.cpp.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace t13
{
inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kRadToDeg = 57.2957795f;

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// The game-basis forward of a yaw/pitch pair (world angles: x forward at yaw 0, z up).
inline Vec3 ForwardFromYawPitch(float yaw, float pitch)
{
    const float cp = std::cos(pitch);
    return {cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch)};
}

// Both controllers' own reading of a facing vector: yaw = atan2(y, x), pitch = atan2(z, |xy|). False for a
// non-finite or (near) zero vector.
inline bool YawPitchFromVector(const Vec3& v, float& yaw, float& pitch)
{
    if (!Finite(v))
        return false;
    const float xy = std::sqrt(v.x * v.x + v.y * v.y);
    if (xy * xy + v.z * v.z < 1e-12f)
        return false;
    yaw = std::atan2(v.y, v.x);
    pitch = std::atan2(v.z, xy);
    return true;
}

// The angle between two directions, radians. NaN when either is non-finite or (near) zero.
inline float AngleBetween(const Vec3& a, const Vec3& b)
{
    if (!Finite(a) || !Finite(b))
        return std::numeric_limits<float>::quiet_NaN();
    const float la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    const float lb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    if (!(la > 1e-6f) || !(lb > 1e-6f))
        return std::numeric_limits<float>::quiet_NaN();
    float c = (a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb);
    c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    return std::acos(c);
}

// An angle onto (-pi, pi]. Non-finite passes through.
inline float WrapPi(float a) { return std::isfinite(a) ? static_cast<float>(std::remainder(a, 2.0f * kPi)) : a; }

// The game tick seen by consecutive controller calls. Step is true on the FIRST call of a new tick; repeat
// calls in the same tick (several dispatcher callers) return false. A tick that goes backwards, or jumps
// by more than maxGap, also sets *discontinuity (a load, a revert: a new stint). Differences wrap like
// the game's own 32-bit `sub`, so a tick counter crossing INT32_MAX is an ordinary step.
struct TickGate
{
    int32_t last = 0;
    bool have = false;

    bool Step(int32_t tick, bool* discontinuity, int32_t maxGap = 60)
    {
        if (discontinuity)
            *discontinuity = false;
        if (!have)
        {
            have = true;
            last = tick;
            return true;
        }
        const int32_t d = static_cast<int32_t>(static_cast<uint32_t>(tick) - static_cast<uint32_t>(last));
        if (d == 0)
            return false;
        if ((d < 0 || d > maxGap) && discontinuity)
            *discontinuity = true;
        last = tick;
        return true;
    }
    void Reset() { *this = TickGate{}; }
};

// The known return addresses of a dispatcher's callers. Index(r) is r's slot, or N for an unknown (or 0)
// address; a slot whose proof failed at install holds 0 and never matches.
template <size_t N>
struct CallerTable
{
    uintptr_t ret[N]{};
    size_t Index(uintptr_t r) const
    {
        if (!r)
            return N;
        for (size_t i = 0; i < N; ++i)
            if (ret[i] == r)
                return i;
        return N;
    }
};

// Running max and mean of a non-negative quantity; non-finite samples are skipped.
struct MaxMean
{
    float max = 0.0f;
    double sum = 0.0;
    uint32_t n = 0;
    void Add(float v)
    {
        if (!std::isfinite(v))
            return;
        if (v > max)
            max = v;
        sum += v;
        ++n;
    }
    float Mean() const { return n ? static_cast<float>(sum / n) : 0.0f; }
    void Reset() { *this = MaxMean{}; }
};

// The craft's turn rate from one tick's FINAL desired angles to the next, per second of game time (for the
// later setpoint slew cap: docs section 4, R4/H3). A tick is excluded (counted) when the caller says so (a
// stint's first tick, a forced-steer window), when there is no previous final, or when the angles or the
// seconds per tick are not usable. The stick's own arguments are tracked beside it.
struct RateWindow
{
    float maxTurn = 0.0f, maxYaw = 0.0f, maxPitch = 0.0f;   // rad/s
    float maxArgYaw = 0.0f, maxArgPitch = 0.0f;             // |yawD| / dtA, |pitchD| / dtA
    uint32_t ticks = 0, excluded = 0;
    bool havePrev = false;
    float prevYaw = 0.0f, prevPitch = 0.0f;

    void CloseTick(float yaw, float pitch, float spt, bool exclude)
    {
        const bool valid = std::isfinite(yaw) && std::isfinite(pitch);
        if (exclude || !havePrev || !valid || !(spt > 0.0f) || !std::isfinite(spt))
            ++excluded;
        else
        {
            const float turn = AngleBetween(ForwardFromYawPitch(prevYaw, prevPitch), ForwardFromYawPitch(yaw, pitch)) / spt;
            const float dy = std::fabs(WrapPi(yaw - prevYaw)) / spt;
            const float dp = std::fabs(pitch - prevPitch) / spt;
            if (turn > maxTurn)
                maxTurn = turn;
            if (dy > maxYaw)
                maxYaw = dy;
            if (dp > maxPitch)
                maxPitch = dp;
            ++ticks;
        }
        havePrev = valid;
        prevYaw = yaw;
        prevPitch = pitch;
    }
    void AddArgs(float yawD, float pitchD, float dtA)
    {
        if (!(dtA > 0.0f) || !std::isfinite(dtA))
            return;
        if (std::isfinite(yawD) && std::fabs(yawD) / dtA > maxArgYaw)
            maxArgYaw = std::fabs(yawD) / dtA;
        if (std::isfinite(pitchD) && std::fabs(pitchD) / dtA > maxArgPitch)
            maxArgPitch = std::fabs(pitchD) / dtA;
    }
    // A new reporting window: the maxima and counts restart, the previous final is kept.
    void ResetWindow()
    {
        maxTurn = maxYaw = maxPitch = maxArgYaw = maxArgPitch = 0.0f;
        ticks = excluded = 0;
    }
    void Reset() { *this = RateWindow{}; }
};

// A value that is logged when it changes. The first observation after a Reset counts as a change, so a
// stint states its starting value once.
template <class T>
struct ChangeWatch
{
    T last{};
    bool have = false;
    bool Changed(T v, T* was = nullptr)
    {
        const bool changed = Differs(v);
        if (was)
            *was = have ? last : v;
        Set(v);
        return changed;
    }
    // The two halves, for a caller that may hold a change back and log the latest value later.
    bool Differs(T v) const { return !have || !(v == last); }
    void Set(T v)
    {
        have = true;
        last = v;
    }
    void Reset() { *this = ChangeWatch{}; }
};

// The controllers' own forced-steer/redirect window test, exactly as compiled (Reach 0x64E58, Halo 4
// 0xAAB35): `sub tick, stamp; cvtdq2ps; mulss spt; comiss window; jbe closed`. Open while
// window > (tick - stamp) * spt. The tick difference wraps like the 32-bit `sub` (never signed overflow);
// stamp -1 means none (the controller skips it, Reach 0x64E4E); NaN anywhere compares unordered: closed.
inline int32_t TickDelta(int32_t tick, int32_t stamp)
{
    return static_cast<int32_t>(static_cast<uint32_t>(tick) - static_cast<uint32_t>(stamp));
}
inline bool WindowOpen(int32_t tick, int32_t stamp, float spt, float window)
{
    if (stamp == -1)
        return false;
    return window > static_cast<float>(TickDelta(tick, stamp)) * spt;
}
inline float WindowAge(int32_t tick, int32_t stamp, float spt)
{
    return stamp == -1 ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(TickDelta(tick, stamp)) * spt;
}

// WHICH CALLER RAN: the dispatcher's own return address, read from the callee's entry stack. A call from
// the dispatcher body lands with rsp = dispatcher entry rsp - frame - 8, where frame = pushes * 8 + the
// `sub rsp` immediate, so the dispatcher's return address sits at callee entry rsp + frame + 8. Both
// dispatchers have fixed frames (no rsp arithmetic outside the prologue; measured), so the slot is derived
// here from the prologue bytes the install has already proven exact, instead of being a hardcoded 0x100.
// Understood forms: `mov rax, rsp`; `mov [rax+disp8], r64` and `lea rbp, [rax+disp8|disp32]` (rsp
// unchanged); `push r64` (50-57, 41 50-57); then `sub rsp, imm8|imm32`, which ends the prologue. Anything
// else, or a frame that is not a whole number of slots: false (caller attribution stays off).
inline bool DispatcherReturnSlot(const uint8_t* p, size_t n, uint32_t& slot)
{
    slot = 0;
    if (!p)
        return false;
    uint32_t pushes = 0;
    size_t i = 0;
    while (i < n)
    {
        const uint8_t b = p[i];
        if (b >= 0x50 && b <= 0x57)
        {
            ++pushes;
            i += 1;
            continue;
        }
        if (b == 0x41 && i + 1 < n && p[i + 1] >= 0x50 && p[i + 1] <= 0x57)
        {
            ++pushes;
            i += 2;
            continue;
        }
        if (i + 2 >= n)
            return false;
        const uint8_t op = p[i + 1], modrm = p[i + 2];
        if (b == 0x48 && op == 0x8B && modrm == 0xC4)   // mov rax, rsp
        {
            i += 3;
            continue;
        }
        if ((b == 0x48 || b == 0x4C) && op == 0x89 && (modrm & 0xC7) == 0x40)   // mov [rax+disp8], r64
        {
            i += 4;
            continue;
        }
        if (b == 0x48 && op == 0x8D && modrm == 0x68)   // lea rbp, [rax+disp8]
        {
            i += 4;
            continue;
        }
        if (b == 0x48 && op == 0x8D && modrm == 0xA8)   // lea rbp, [rax+disp32]
        {
            i += 7;
            continue;
        }
        if (b == 0x48 && (op == 0x81 || op == 0x83) && modrm == 0xEC)   // sub rsp, imm32 | imm8
        {
            uint32_t sub = 0;
            if (op == 0x81)
            {
                if (i + 7 > n)
                    return false;
                sub = static_cast<uint32_t>(p[i + 3]) | (static_cast<uint32_t>(p[i + 4]) << 8) |
                      (static_cast<uint32_t>(p[i + 5]) << 16) | (static_cast<uint32_t>(p[i + 6]) << 24);
            }
            else
            {
                if (i + 4 > n)
                    return false;
                sub = p[i + 3];
            }
            const uint64_t bytes = static_cast<uint64_t>(pushes) * 8u + sub + 8u;
            if (bytes % 8u || bytes > 0x10000u)
                return false;
            slot = static_cast<uint32_t>(bytes / 8u);
            return true;
        }
        return false;
    }
    return false;
}

// THE OWNER THREAD. Only one thread at a time observes a lane (the ring below has one producer). The first
// caller claims it; another thread may take it over only when the owner has been silent for more than
// takeoverMs (a sim-thread change, a level reload), never while it is calling. The new owner must reset
// every producer-only state before its first push. Claims race through compare_exchange: one wins.
enum class Claim : uint8_t
{
    Owner,      // the caller already owns the lane
    NewOwner,   // claimed now: reset the producer state first
    Other       // another thread owns it: do not observe (counted)
};

class OwnerClaim
{
public:
    // thread must be nonzero (a Windows thread id).
    Claim Step(uint32_t thread, uint64_t nowMs, uint64_t takeoverMs = 500, uint32_t* previous = nullptr)
    {
        uint32_t current = owner_.load(std::memory_order_acquire);
        if (previous)
            *previous = current;
        if (!thread)
            return Claim::Other;
        if (current == thread)
        {
            lastMs_.store(nowMs, std::memory_order_release);
            return Claim::Owner;
        }
        if (current != 0)
        {
            const uint64_t last = lastMs_.load(std::memory_order_acquire);
            if (!(nowMs >= last && nowMs - last > takeoverMs))
                return Claim::Other;
        }
        // Stamp first: a thread reading the new owner must never see the old owner's stale time and take
        // the lane straight back. A lost race only extends the winner's lease.
        lastMs_.store(nowMs, std::memory_order_release);
        if (!owner_.compare_exchange_strong(current, thread, std::memory_order_acq_rel, std::memory_order_acquire))
            return Claim::Other;
        return Claim::NewOwner;
    }
    uint32_t Current() const { return owner_.load(std::memory_order_acquire); }
    void Reset()
    {
        owner_.store(0, std::memory_order_release);
        lastMs_.store(0, std::memory_order_release);
    }

private:
    std::atomic<uint32_t> owner_{0};
    std::atomic<uint64_t> lastMs_{0};
};

// A lock-free single-producer / single-consumer ring of trivially copyable events. Indices are free-running
// 32-bit counters (they wrap past 2^32 without harm: N is a power of two no larger than 2^31). A full ring
// drops the new event and counts it.
template <class T, size_t N>
class SpscRing
{
    static_assert(N >= 2 && (N & (N - 1)) == 0 && N <= (size_t(1) << 31), "N: a power of two, at most 2^31");
    static_assert(std::is_trivially_copyable_v<T>, "events are copied as bytes");

public:
    bool Push(const T& value)   // producer only
    {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= N)
        {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[head & (N - 1)] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    bool Pop(T& value)   // consumer only
    {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head)
            return false;
        value = slots_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    uint32_t Size() const
    {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    uint32_t TakeDropped() { return dropped_.exchange(0, std::memory_order_relaxed); }
    // Tests only: an empty ring whose indices start at `index` (e.g. just below 2^32).
    void ResetIndicesForTest(uint32_t index)
    {
        head_.store(index, std::memory_order_relaxed);
        tail_.store(index, std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> head_{0}, tail_{0}, dropped_{0};
    T slots_[N]{};
};

// ---------------------------------------------------------------------------------------------------------
// OBSERVATIONS. The hook reads one Sample per controller call (under SEH, title-specific offsets) and hands
// it to a Producer, which keeps the per-tick and per-stint bookkeeping and pushes Events; the cold worker
// formats them. Every field the title cannot supply keeps its "unread" default.
// ---------------------------------------------------------------------------------------------------------
inline constexpr int32_t kUnread = std::numeric_limits<int32_t>::min();

struct Sample
{
    uint32_t vehicle = 0;
    uint16_t user = 0;
    int32_t tick = 0;
    float spt = std::numeric_limits<float>::quiet_NaN();   // game seconds per tick
    uint64_t nowMs = 0;
    float yawD = 0.0f, pitchD = 0.0f, dtA = 0.0f, dtB = 0.0f;   // the controller's arguments
    float pre[3]{}, post[3]{};   // desired yaw / pitch / roll before and after the native controller
    bool rollMode = false;       // record flag bit 2 (as the native read it, before the call)
    float moveX = 0.0f;          // input+4, the roll axis
    float idle = 0.0f, rollRate = 0.0f, hold = 0.0f;   // the controller's timers, after the call
    uint32_t latch = 0;
    bool haveVehicle = false;
    Vec3 facing{};               // the desired facing the hull/physics steers by
    float rollField = std::numeric_limits<float>::quiet_NaN();   // the vehicle's roll the physics reads
    Vec3 craftFwd{};             // the vehicle field read with the hull's atan2 pattern ([I] the nose)
    int32_t stamp = -1;          // forced-steer / redirect stamp tick (-1 none)
    Vec3 stampDir{};
    // Reach only.
    Vec3 aim{};                  // veh+0x214, the aiming vector the forced steer projects
    int32_t state = kUnread, stateOff = kUnread;   // veh + i16[veh+0x1AE] (0x1B: hull steering skipped)
    // Halo 4 only.
    int32_t gateBit = -1;        // veh+0x16C bit 2: the driver->vehicle facing copy is skipped
    bool haveUnitFacing = false;
    Vec3 unitFacing{};           // the pilot unit's +0x4BC, the value the copy carries to the vehicle
    uint32_t unit = 0xFFFFFFFFu, parent = 0xFFFFFFFFu;
    int32_t seat = -1;
    int32_t physOff = kUnread, physMode = kUnread;   // the pilot biped's physics mode (7/8: rebased angles)
    int32_t overrideSet = kUnread, overrideType = kUnread;   // veh+0xF58 / +0xF59
    int32_t redirectFlag = kUnread;                          // veh+0x105C
};

// What the stint's one-shot tag read found; the producer keeps the window and the project bit for the stint.
struct TagFacts
{
    bool valid = false;
    float window = std::numeric_limits<float>::quiet_NaN();
    int32_t projBit = -1;
    int32_t tagType = kUnread;
};

enum class Kind : uint8_t
{
    StintBegin,
    StintEnd,
    Link,
    Window,
    Tag,
    Stamp,
    Roll,
    Dt,
    State,
    Phys,
    Type,
    Owner,
    Fault
};

enum class StintEndReason : uint8_t
{
    Vehicle,         // another vehicle handle
    Discontinuity,   // the tick went backwards or jumped
    Gap,             // no call for more than the stint gap
    Owner,           // another thread took the lane over
    Fault,           // a read faulted
    Retired          // the hook was removed and is being reinstalled while the stint was still open
};

struct StintData
{
    uint32_t thread, ticks, calls;
    uint8_t reason;
};
struct LinkData
{
    float pre[3], post[3];
    float facingYaw, facingPitch;
    float err, errPrev;        // rad: f(post) and f(pre) against the facing
    float rollField, dRoll;    // dRoll = WrapPi(rollField - post roll), rad
    float unitErr;             // rad: f(post) against the pilot unit's facing (Halo 4)
    int32_t gateBit;
    float craftErr;            // rad: craftFwd against the facing
};
struct WindowData
{
    float seconds;
    uint32_t linkN;
    float errMax, errMean, errPrevMax, errPrevMean, dRollMax, unitErrMax, craftMax, craftMean;   // rad
    float maxTurn, maxYaw, maxPitch, maxArgYaw, maxArgPitch;                                       // rad/s
    uint32_t rateTicks, rateExcluded, windowTicks;
    uint32_t heldBack;     // change lines held back by the per-kind rate limit (their latest value follows)
};
struct TagData
{
    uint8_t valid;
    uint16_t def;
    int32_t n13, tagType;
    float ctl[11];         // the controller's limits (Reach sf+0x5C..+0x84, Halo 4 T+0x60..+0x88)
    uint32_t projWord;     // Reach sf+0xA8, Halo 4 T+0xC8 (bit 0)
    float window;          // Reach sf+0xAC, Halo 4 T+0xCC
    float phys[6];         // Reach hull sf+0x10/14/38/88/8C/90; Halo 4 physics T+0x10/14/3C/A8 (then NaN)
    uint32_t physWord;     // Halo 4 physics T+0xA4 (bit 0)
    float vehScale;        // Reach veh+0x3A0, Halo 4 veh+0x6A4
};
struct StampData
{
    int32_t stamp, was;
    float age, window;
    uint8_t open;
    int32_t projBit, flag;
    Vec3 dir, aim;
    float nativeJump;      // rad: how far this call moved the desired forward
};
struct RollData
{
    uint8_t bit2;
    float moveX, rate, idle, hold;
    uint32_t latch;
};
struct DtData
{
    float dtA, dtB, spt;
};
struct StateData
{
    int32_t state, was, off;
};
struct PhysData
{
    uint32_t unit;
    int32_t off, mode, was;
};
struct TypeData
{
    int32_t tagType, f58, f59;
};
struct OwnerData
{
    uint32_t from, to;
};
struct FaultData
{
    uint32_t where;   // 0 before the native, 1 after, 2 the tag read
};

struct Event
{
    uint8_t title = 0;   // the lane: 0 Reach, 1 Halo 4
    Kind kind = Kind::Fault;
    uint16_t user = 0;
    uint32_t vehicle = 0;
    int32_t tick = 0;
    union
    {
        StintData stint;
        LinkData link;
        WindowData window;
        TagData tag;
        StampData stampEv;
        RollData roll;
        DtData dt;
        StateData state;
        PhysData phys;
        TypeData type;
        OwnerData owner;
        FaultData fault;
    } u{};
};
static_assert(std::is_trivially_copyable_v<Event>);

// THE PRODUCER: one per lane, touched only by whoever holds the lane's busy flag -- the owner thread on each
// call, and the cold worker to end a stint whose calls have stopped (EndIfStale). Per call: Begin (tick gate,
// stint begin/end), then the hook reads the tag if Begin asks, then Finish (per-tick events).
struct CallStep
{
    bool newTick = false;      // the first call of a game tick (or of a stint)
    bool stintBegan = false;
    bool needTag = false;      // read the stint's tag once, then SetTag
};

template <size_t RingN>
class Producer
{
public:
    static constexpr uint64_t kStintGapMs = 500;     // no call for this long: the stint is over
    static constexpr uint32_t kLinkBurstTicks = 30;  // per-tick LINK lines at a stint's start
    static constexpr uint64_t kWindowMs = 2000;      // LINK / RATE summaries
    static constexpr uint64_t kChangeLineMs = 250;   // at most one change line per kind this often
    static_assert(static_cast<size_t>(Kind::Fault) < 16, "lastLineMs_ is indexed by Kind");

    explicit Producer(uint8_t title) : title_(title) {}

    // Forget everything (a new owner, an install). No event.
    void Reset()
    {
        const uint8_t title = title_;
        *this = Producer(title);
    }

    // Ends the stint now (owner change, fault), with its summary. Safe when none is active.
    void EndStint(StintEndReason reason, SpscRing<Event, RingN>& ring)
    {
        if (!active_)
            return;
        FlushWindow(ring, lastCallMs_);
        Event e = Make(Kind::StintEnd, vehicle_, user_, lastTick_);
        e.u.stint = {thread_, stintTicks_, stintCalls_, static_cast<uint8_t>(reason)};
        ring.Push(e);
        active_ = false;
    }

    // The worker's half: a stint whose last call is more than kStintGapMs before nowMs is over (a landing,
    // leaving the craft, the level ending), so it ends now with its last partial window, instead of at the next
    // call, which may come much later or never. The same rule Begin applies, so the next call simply begins a
    // new stint. True when it ended one; a clock that reads earlier than the last call ends nothing.
    bool EndIfStale(uint64_t nowMs, SpscRing<Event, RingN>& ring)
    {
        if (!active_ || !(nowMs >= lastCallMs_ && nowMs - lastCallMs_ > kStintGapMs))
            return false;
        EndStint(StintEndReason::Gap, ring);
        return true;
    }

    // A stint whose reads faulted observes nothing more until the next stint: true (and the stint kept
    // alive) while calls for the faulted vehicle keep coming without a gap.
    bool SkipFaulted(uint32_t vehicle, uint64_t nowMs)
    {
        if (!faultedVehicle_ || faultedVehicle_ != vehicle || !(nowMs >= lastCallMs_ && nowMs - lastCallMs_ <= kStintGapMs))
            return false;
        lastCallMs_ = nowMs;
        return true;
    }
    void MarkFaulted(uint32_t vehicle, uint64_t nowMs, uint32_t where, SpscRing<Event, RingN>& ring)
    {
        Event e = Make(Kind::Fault, vehicle, user_, lastTick_);
        e.u.fault = {where};
        ring.Push(e);
        EndStint(StintEndReason::Fault, ring);
        faultedVehicle_ = vehicle;
        lastCallMs_ = nowMs;
    }

    CallStep Begin(const Sample& s, uint32_t thread, SpscRing<Event, RingN>& ring)
    {
        CallStep step;
        bool disc = false;
        const bool newTick = gate_.Step(s.tick, &disc);
        const bool gap = !(s.nowMs >= lastCallMs_ && s.nowMs - lastCallMs_ <= kStintGapMs);
        if (!active_ || s.vehicle != vehicle_ || disc || gap)
        {
            if (active_)
                EndStint(s.vehicle != vehicle_ ? StintEndReason::Vehicle
                                                : disc ? StintEndReason::Discontinuity : StintEndReason::Gap,
                         ring);
            BeginStint(s, thread, ring);
            step.stintBegan = true;
            step.newTick = true;
        }
        else
            step.newTick = newTick;
        lastCallMs_ = s.nowMs;
        faultedVehicle_ = 0;
        ++stintCalls_;
        step.needTag = !tagDone_;
        return step;
    }

    // The hook's one-shot tag read for this stint (valid or not: it is asked once).
    void SetTag(const TagFacts& facts)
    {
        tag_ = facts;
        tagDone_ = true;
    }

    void Finish(const Sample& s, const CallStep& step, SpscRing<Event, RingN>& ring)
    {
        const bool open = tag_.valid && WindowOpen(s.tick, s.stamp, s.spt, tag_.window);
        rate_.AddArgs(s.yawD, s.pitchD, s.dtA);
        if (step.newTick)
        {
            // The previous tick is over: its final angles are what this first call found (s.pre).
            if (!step.stintBegan)
                rate_.CloseTick(s.pre[0], s.pre[1], s.spt, tickWindowOpen_);
            else
                rate_.CloseTick(s.pre[0], s.pre[1], s.spt, true);
            tickWindowOpen_ = false;
            ++stintTicks_;
            if (s.haveVehicle)
                Link(s, ring);
            Watches(s, step, ring);
        }
        if (open)
        {
            if (!tickWindowOpen_)
                ++windowTicks_;
            tickWindowOpen_ = true;
        }
        if (s.nowMs >= windowStartMs_ && s.nowMs - windowStartMs_ >= kWindowMs)
            FlushWindow(ring, s.nowMs);
        lastTick_ = s.tick;
    }

    bool Active() const { return active_; }
    uint32_t Vehicle() const { return vehicle_; }
    const TagFacts& Tag() const { return tag_; }
    // The latest facing and craft forward, for the worker's camera line (yaw/pitch, rad; NaN = unread).
    float facingYaw = std::numeric_limits<float>::quiet_NaN(), facingPitch = std::numeric_limits<float>::quiet_NaN();
    float craftYaw = std::numeric_limits<float>::quiet_NaN(), craftPitch = std::numeric_limits<float>::quiet_NaN();

private:
    static bool SameWithin(float x, float y, float eps)
    {
        if (std::isnan(x) || std::isnan(y))
            return std::isnan(x) && std::isnan(y);
        return std::fabs(x - y) <= eps;
    }

    Event Make(Kind kind, uint32_t vehicle, uint16_t user, int32_t tick) const
    {
        Event e{};
        e.title = title_;
        e.kind = kind;
        e.user = user;
        e.vehicle = vehicle;
        e.tick = tick;
        return e;
    }

    void BeginStint(const Sample& s, uint32_t thread, SpscRing<Event, RingN>& ring)
    {
        const uint8_t title = title_;
        const uint64_t lastCall = lastCallMs_;
        *this = Producer(title);
        lastCallMs_ = lastCall;
        gate_.Step(s.tick, nullptr);
        active_ = true;
        vehicle_ = s.vehicle;
        user_ = s.user;
        thread_ = thread;
        lastTick_ = s.tick;
        windowStartMs_ = s.nowMs;
        Event e = Make(Kind::StintBegin, s.vehicle, s.user, s.tick);
        e.u.stint = {thread, 0, 0, 0};
        ring.Push(e);
    }

    void Link(const Sample& s, SpscRing<Event, RingN>& ring)
    {
        LinkData d{};
        for (int i = 0; i < 3; ++i)
        {
            d.pre[i] = s.pre[i];
            d.post[i] = s.post[i];
        }
        if (!YawPitchFromVector(s.facing, d.facingYaw, d.facingPitch))
            d.facingYaw = d.facingPitch = std::numeric_limits<float>::quiet_NaN();
        facingYaw = d.facingYaw;
        facingPitch = d.facingPitch;
        if (!YawPitchFromVector(s.craftFwd, craftYaw, craftPitch))
            craftYaw = craftPitch = std::numeric_limits<float>::quiet_NaN();
        d.err = AngleBetween(ForwardFromYawPitch(s.post[0], s.post[1]), s.facing);
        d.errPrev = AngleBetween(ForwardFromYawPitch(s.pre[0], s.pre[1]), s.facing);
        d.rollField = s.rollField;
        d.dRoll = WrapPi(s.rollField - s.post[2]);
        d.unitErr = s.haveUnitFacing ? AngleBetween(ForwardFromYawPitch(s.post[0], s.post[1]), s.unitFacing)
                                     : std::numeric_limits<float>::quiet_NaN();
        d.gateBit = s.gateBit;
        d.craftErr = AngleBetween(s.craftFwd, s.facing);
        err_.Add(d.err);
        errPrev_.Add(d.errPrev);
        dRoll_.Add(std::fabs(d.dRoll));
        unitErr_.Add(d.unitErr);
        craft_.Add(d.craftErr);
        if (linkBurst_ < kLinkBurstTicks)
        {
            ++linkBurst_;
            Event e = Make(Kind::Link, s.vehicle, s.user, s.tick);
            e.u.link = d;
            ring.Push(e);
        }
    }

    // CHANGE LINES. A value is logged when it differs from the last value LOGGED, at most once per kind every
    // kChangeLineMs: a stamp the game re-writes every tick while the craft scrapes a boundary must not flood
    // the log. A held-back change is not lost -- the watch keeps the last logged value, so the latest one goes
    // out when the hold-off ends -- and the window summary counts the held-back ticks. A stint's first values
    // always go out.
    bool Allow(Kind kind, uint64_t nowMs, bool stintStart)
    {
        uint64_t& last = lastLineMs_[static_cast<size_t>(kind)];
        if (!stintStart && last && nowMs >= last && nowMs - last < kChangeLineMs)
        {
            ++heldBack_;
            return false;
        }
        last = nowMs ? nowMs : 1;
        return true;
    }

    void Watches(const Sample& s, const CallStep& step, SpscRing<Event, RingN>& ring)
    {
        const bool first = step.stintBegan;
        if (s.haveVehicle)
        {
            if (stamp_.Differs(s.stamp) && Allow(Kind::Stamp, s.nowMs, first))
            {
                const int32_t was = stamp_.have ? stamp_.last : kUnread;
                stamp_.Set(s.stamp);
                Event e = Make(Kind::Stamp, s.vehicle, s.user, s.tick);
                StampData& d = e.u.stampEv;
                d.stamp = s.stamp;
                d.was = first ? kUnread : was;
                d.age = WindowAge(s.tick, s.stamp, s.spt);
                d.window = tag_.window;
                d.open = (tag_.valid && WindowOpen(s.tick, s.stamp, s.spt, tag_.window)) ? 1 : 0;
                d.projBit = tag_.projBit;
                d.flag = s.redirectFlag;
                d.dir = s.stampDir;
                d.aim = s.aim;
                d.nativeJump = AngleBetween(ForwardFromYawPitch(s.pre[0], s.pre[1]), ForwardFromYawPitch(s.post[0], s.post[1]));
                ring.Push(e);
            }
            if ((s.state != kUnread || s.stateOff != kUnread) && state_.Differs(s.state) &&
                Allow(Kind::State, s.nowMs, first))
            {
                const int32_t was = state_.have ? state_.last : kUnread;
                state_.Set(s.state);
                Event e = Make(Kind::State, s.vehicle, s.user, s.tick);
                e.u.state = {s.state, first ? kUnread : was, s.stateOff};
                ring.Push(e);
            }
            if (s.overrideSet != kUnread)
            {
                const int64_t packed = (static_cast<int64_t>(s.overrideSet) << 32) | static_cast<uint32_t>(s.overrideType);
                if (override_.Differs(packed) && Allow(Kind::Type, s.nowMs, first))
                {
                    override_.Set(packed);
                    Event e = Make(Kind::Type, s.vehicle, s.user, s.tick);
                    e.u.type = {tag_.tagType, s.overrideSet, s.overrideType};
                    ring.Push(e);
                }
            }
        }
        // Roll mode: on a change of the bit, or the roll axis crossing 0.1 while it is set.
        const bool axis = s.rollMode && std::fabs(s.moveX) > 0.1f;
        if ((rollBit_.Differs(s.rollMode) || rollAxis_.Differs(axis)) && Allow(Kind::Roll, s.nowMs, first))
        {
            rollBit_.Set(s.rollMode);
            rollAxis_.Set(axis);
            Event e = Make(Kind::Roll, s.vehicle, s.user, s.tick);
            e.u.roll = {static_cast<uint8_t>(s.rollMode ? 1 : 0), s.moveX, s.rollRate, s.idle, s.hold, s.latch};
            ring.Push(e);
        }
        // The two time arguments, once per stint and whenever either's relation to the tick changes.
        const float a = s.dtA - s.spt, b = s.dtB - s.dtA;
        if ((!dtLogged_ || !SameWithin(a, dtA_, 1e-5f) || !SameWithin(b, dtB_, 1e-5f)) &&
            Allow(Kind::Dt, s.nowMs, first))
        {
            dtLogged_ = true;
            dtA_ = a;
            dtB_ = b;
            Event e = Make(Kind::Dt, s.vehicle, s.user, s.tick);
            e.u.dt = {s.dtA, s.dtB, s.spt};
            ring.Push(e);
        }
        if ((s.physOff != kUnread || s.physMode != kUnread) && phys_.Differs(s.physMode) &&
            Allow(Kind::Phys, s.nowMs, first))
        {
            const int32_t was = phys_.have ? phys_.last : kUnread;
            phys_.Set(s.physMode);
            Event e = Make(Kind::Phys, s.vehicle, s.user, s.tick);
            e.u.phys = {s.unit, s.physOff, s.physMode, first ? kUnread : was};
            ring.Push(e);
        }
    }

    void FlushWindow(SpscRing<Event, RingN>& ring, uint64_t nowMs = 0)
    {
        if (err_.n || rate_.ticks || rate_.excluded || heldBack_)
        {
            Event e = Make(Kind::Window, vehicle_, user_, lastTick_);
            WindowData& d = e.u.window;
            d.seconds = (nowMs && nowMs >= windowStartMs_) ? static_cast<float>(nowMs - windowStartMs_) / 1000.0f
                                                           : std::numeric_limits<float>::quiet_NaN();
            d.linkN = err_.n;
            d.errMax = err_.max;
            d.errMean = err_.Mean();
            d.errPrevMax = errPrev_.max;
            d.errPrevMean = errPrev_.Mean();
            d.dRollMax = dRoll_.max;
            d.unitErrMax = unitErr_.n ? unitErr_.max : std::numeric_limits<float>::quiet_NaN();
            d.craftMax = craft_.max;
            d.craftMean = craft_.Mean();
            d.maxTurn = rate_.maxTurn;
            d.maxYaw = rate_.maxYaw;
            d.maxPitch = rate_.maxPitch;
            d.maxArgYaw = rate_.maxArgYaw;
            d.maxArgPitch = rate_.maxArgPitch;
            d.rateTicks = rate_.ticks;
            d.rateExcluded = rate_.excluded;
            d.windowTicks = windowTicks_;
            d.heldBack = heldBack_;
            ring.Push(e);
        }
        heldBack_ = 0;
        err_.Reset();
        errPrev_.Reset();
        dRoll_.Reset();
        unitErr_.Reset();
        craft_.Reset();
        rate_.ResetWindow();
        windowTicks_ = 0;
        if (nowMs)
            windowStartMs_ = nowMs;
    }

    uint8_t title_;
    TickGate gate_{};
    bool active_ = false;
    uint32_t vehicle_ = 0;
    uint16_t user_ = 0;
    uint32_t thread_ = 0;
    int32_t lastTick_ = 0;
    uint64_t lastCallMs_ = 0;
    uint32_t faultedVehicle_ = 0;
    uint32_t stintTicks_ = 0, stintCalls_ = 0;
    bool tagDone_ = false;
    TagFacts tag_{};
    uint32_t linkBurst_ = 0;
    MaxMean err_{}, errPrev_{}, dRoll_{}, unitErr_{}, craft_{};
    RateWindow rate_{};
    bool tickWindowOpen_ = false;
    uint32_t windowTicks_ = 0;
    uint64_t windowStartMs_ = 0;
    ChangeWatch<int32_t> stamp_{}, state_{}, phys_{};
    ChangeWatch<int64_t> override_{};
    ChangeWatch<bool> rollBit_{}, rollAxis_{};
    bool dtLogged_ = false;
    float dtA_ = 0.0f, dtB_ = 0.0f;
    uint64_t lastLineMs_[16]{};
    uint32_t heldBack_ = 0;
};
} // namespace t13
