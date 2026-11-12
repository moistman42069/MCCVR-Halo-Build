// Type-13 flight-controller observer (src/common/type13_flight_log.h): the engine-free half of the dev-only
// log hooks on haloreach+0x64828 and halo4+0xAA524 -- the tick gate, the forced-steer window as the
// controllers compile it, the caller frame derived from the dispatcher prologue, the owner-thread claim, the
// single-producer ring and the per-stint producer.
#include "type13_flight_log.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

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
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

using namespace t13;

void TestGeometry()
{
    const float yaws[]{0.0f, 1.0f, -2.5f, 3.1f};
    const float pitches[]{0.0f, 0.7f, -1.4f, 1.4f};
    bool roundTrip = true;
    for (float y : yaws)
        for (float p : pitches)
        {
            float yy = 0.0f, pp = 0.0f;
            const bool ok = YawPitchFromVector(ForwardFromYawPitch(y, p), yy, pp);
            roundTrip &= ok && Near(WrapPi(yy - y), 0.0f) && Near(pp, p);
        }
    Check(roundTrip, "yaw/pitch -> forward -> yaw/pitch round-trips, pitch +-1.4 included");
    float y = 0.0f, p = 0.0f;
    Check(!YawPitchFromVector({0.0f, 0.0f, 0.0f}, y, p), "a zero vector has no yaw/pitch");
    Check(!YawPitchFromVector({NAN, 0.0f, 1.0f}, y, p), "a NaN vector has no yaw/pitch");

    Check(Near(AngleBetween({1, 0, 0}, {2, 0, 0}), 0.0f), "identical directions: 0");
    Check(Near(AngleBetween({1, 0, 0}, {-3, 0, 0}), kPi), "antiparallel directions: pi");
    Check(Near(AngleBetween({1, 0, 0}, {0, 0, 5}), kPi / 2), "perpendicular: pi/2");
    Check(std::isnan(AngleBetween({0, 0, 0}, {1, 0, 0})), "a zero vector: NaN");
    Check(std::isnan(AngleBetween({1, 0, 0}, {INFINITY, 0, 0})), "a non-finite vector: NaN");
    Check(Near(WrapPi(3 * kPi / 2), -kPi / 2) && Near(WrapPi(-3 * kPi / 2), kPi / 2), "WrapPi folds onto (-pi, pi]");
}

void TestTickGate()
{
    TickGate g;
    bool disc = true;
    Check(g.Step(100, &disc) && !disc, "the first call opens a tick");
    Check(!g.Step(100, &disc) && !disc, "a repeat call in the same tick does not");
    Check(g.Step(101, &disc) && !disc, "+1 opens the next tick");
    Check(g.Step(99, &disc) && disc, "a tick that goes backwards is a discontinuity");
    Check(g.Step(199, &disc) && disc, "a jump of +100 is a discontinuity");
    Check(g.Step(201, &disc) && !disc, "+2 (a skipped tick) is not");
    TickGate w;
    w.Step(INT32_MAX - 1, &disc);
    Check(w.Step(INT32_MAX, &disc) && !disc && w.Step(INT32_MIN, &disc) && !disc,
          "crossing INT32_MAX is an ordinary step (32-bit wrap, no overflow)");
}

void TestCallerTable()
{
    CallerTable<3> t;
    t.ret[0] = 0x1000;
    t.ret[2] = 0x3000;   // slot 1's proof failed: 0
    Check(t.Index(0x1000) == 0 && t.Index(0x3000) == 2, "known returns map to their slots");
    Check(t.Index(0x2000) == 3 && t.Index(0) == 3, "an unknown or zero return is N (a failed slot never matches)");
}

void TestRateWindow()
{
    RateWindow r;
    const float spt = 1.0f / 30.0f;
    r.CloseTick(0.0f, 0.0f, spt, true);
    Check(r.ticks == 0 && r.excluded == 1, "a stint's first tick is excluded");
    r.CloseTick(0.1f, 0.0f, spt, false);
    Check(r.ticks == 1 && Near(r.maxYaw, 3.0f, 1e-3f) && Near(r.maxTurn, 3.0f, 1e-3f), "0.1 rad in one tick = 3 rad/s");
    r.CloseTick(0.5f, 0.0f, spt, true);
    Check(r.ticks == 1 && r.excluded == 2, "a tick in the forced-steer window is excluded");
    r.CloseTick(0.5f, 0.05f, spt, false);
    Check(Near(r.maxPitch, 1.5f, 1e-3f), "pitch rate");
    RateWindow w;
    w.CloseTick(kPi - 0.05f, 0.0f, spt, true);
    w.CloseTick(-kPi + 0.05f, 0.0f, spt, false);
    Check(Near(w.maxYaw, 3.0f, 2e-3f) && Near(w.maxTurn, 3.0f, 2e-3f), "the yaw wrap at +-pi is a 0.1 rad step, not 2pi");
    w.AddArgs(0.2f, -0.1f, 0.0f);
    Check(w.maxArgYaw == 0.0f, "stick args with dtA 0 are skipped");
    w.AddArgs(0.2f, -0.1f, 0.1f);
    Check(Near(w.maxArgYaw, 2.0f) && Near(w.maxArgPitch, 1.0f), "stick args: |delta| / dtA");
    w.ResetWindow();
    Check(w.ticks == 0 && w.maxYaw == 0.0f && w.havePrev, "a new window keeps the previous final");
}

void TestWindow()
{
    const float spt = 0.5f;
    Check(WindowOpen(10, 9, spt, 1.0f), "window 1.0 s, age 0.5 s: open");
    Check(!WindowOpen(10, 8, spt, 1.0f), "age == window: closed (comiss + jbe)");
    Check(!WindowOpen(10, 7, spt, 1.0f), "age past the window: closed");
    Check(!WindowOpen(10, -1, spt, 100.0f), "stamp -1: closed whatever the window");
    Check(!WindowOpen(10, 9, NAN, 1.0f) && !WindowOpen(10, 9, spt, NAN), "NaN compares unordered: closed");
    Check(TickDelta(INT32_MIN + 5, INT32_MAX - 5) == 11, "the tick difference wraps like the game's sub");
    Check(WindowOpen(INT32_MIN + 1, INT32_MAX, spt, 1.5f), "a stamp across INT32_MAX: 2 ticks, still open");
    Check(Near(WindowAge(12, 10, spt), 1.0f) && std::isnan(WindowAge(12, -1, spt)), "the age in seconds; none for -1");
}

void TestChangeWatch()
{
    ChangeWatch<int32_t> w;
    int32_t was = 0;
    Check(w.Changed(-1, &was) && was == -1, "the first observation is a change");
    Check(!w.Changed(-1, &was), "the same value is not");
    Check(w.Changed(57, &was) && was == -1, "a new value is, with the old one");
}

void TestDispatcherFrame()
{
    // Reach 0x6350C and Halo 4 0xA8828, exactly as the install proves them.
    const uint8_t reach[]{0x48, 0x8B, 0xC4, 0x55, 0x53, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
                          0x57, 0x48, 0x8D, 0x68, 0xB1, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};
    const uint8_t h4[]{0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56,
                       0x41, 0x57, 0x48, 0x8D, 0x68, 0xB1, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00};
    uint32_t slot = 0;
    Check(DispatcherReturnSlot(reach, sizeof(reach), slot) && slot == 0x20,
          "Reach: 7 pushes + 0xC0 + the call = entry rsp + 0x100 (slot 0x20)");
    Check(DispatcherReturnSlot(h4, sizeof(h4), slot) && slot == 0x20,
          "Halo 4: mov [rax+20h],rbx moves nothing; 5 pushes + 0xD0 + the call = slot 0x20");
    Check(!DispatcherReturnSlot(reach, sizeof(reach) - 2, slot) && slot == 0, "a truncated sub: no frame");
    const uint8_t unknown[]{0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x10, 0, 0, 0};
    Check(!DispatcherReturnSlot(unknown, sizeof(unknown), slot), "an instruction the parser does not know: no frame");
    const uint8_t small[]{0x53, 0x48, 0x83, 0xEC, 0x20};
    Check(DispatcherReturnSlot(small, sizeof(small), slot) && slot == 6, "push rbx; sub rsp,20h: 8 + 0x20 + 8 = slot 6");
    const uint8_t lea32[]{0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8D, 0xA8, 0x18, 0xFF, 0xFF, 0xFF, 0x48, 0x83, 0xEC, 0x08};
    Check(DispatcherReturnSlot(lea32, sizeof(lea32), slot) && slot == 3, "lea rbp,[rax+disp32] moves nothing");
    Check(!DispatcherReturnSlot(nullptr, 4, slot), "no bytes: no frame");
}

void TestOwnerClaim()
{
    OwnerClaim o;
    uint32_t prev = 99;
    Check(o.Step(10, 1000, 500, &prev) == Claim::NewOwner && prev == 0, "the first caller claims the lane");
    Check(o.Step(10, 1100) == Claim::Owner, "the owner keeps it");
    Check(o.Step(20, 1200) == Claim::Other, "another thread is refused while the owner calls");
    Check(o.Step(20, 1600) == Claim::Other, "...at exactly 500 ms of silence too");
    Check(o.Step(20, 1601, 500, &prev) == Claim::NewOwner && prev == 10 && o.Current() == 20,
          "after more than 500 ms of silence it takes over");
    Check(o.Step(10, 1700) == Claim::Other, "and the old owner, back, is now refused");
    Check(o.Step(0, 9999) == Claim::Other, "thread id 0 never owns");

    // Racing claims on a fresh lane: exactly one thread wins.
    for (int round = 0; round < 50; ++round)
    {
        OwnerClaim race;
        std::atomic<int> winners{0};
        std::atomic<bool> go{false};
        std::vector<std::thread> threads;
        for (uint32_t t = 1; t <= 4; ++t)
            threads.emplace_back([&, t] {
                while (!go.load())
                {
                }
                if (race.Step(t, 5000) == Claim::NewOwner)
                    winners.fetch_add(1);
            });
        go.store(true);
        for (auto& th : threads)
            th.join();
        if (winners.load() != 1)
        {
            Check(false, "racing claims: exactly one winner");
            return;
        }
    }
    Check(true, "racing claims: exactly one winner in every round");
}

void TestRing()
{
    {
        SpscRing<uint32_t, 4> r;
        bool pushed = true;
        for (uint32_t i = 0; i < 4; ++i)
            pushed &= r.Push(i);
        Check(pushed && r.Size() == 4, "a ring of 4 takes 4");
        Check(!r.Push(99) && r.TakeDropped() == 1 && r.TakeDropped() == 0, "the fifth is dropped and counted once");
        uint32_t v = 0;
        bool order = true;
        for (uint32_t i = 0; i < 4; ++i)
            order &= r.Pop(v) && v == i;
        Check(order && !r.Pop(v), "first in, first out; then empty");
    }
    {
        SpscRing<uint32_t, 8> r;
        r.ResetIndicesForTest(0xFFFFFFFAu);
        bool ok = true;
        uint32_t v = 0;
        for (uint32_t i = 0; i < 40; ++i)
        {
            ok &= r.Push(i) && r.Push(i + 1000);
            ok &= r.Pop(v) && v == i && r.Pop(v) && v == i + 1000;
        }
        Check(ok && r.Size() == 0, "the indices wrap past 2^32 without losing or reordering");
    }
    {
        // Two threads: the consumer sees an increasing sequence and nothing invented; pushed = popped + dropped.
        static SpscRing<uint32_t, 256> r;
        constexpr uint32_t kCount = 400000;
        std::atomic<bool> done{false};
        uint32_t popped = 0, dropped = 0;
        bool increasing = true;
        std::thread consumer([&] {
            uint32_t v = 0, last = 0;
            bool first = true;
            for (;;)
            {
                if (r.Pop(v))
                {
                    increasing &= first || v > last;
                    first = false;
                    last = v;
                    ++popped;
                }
                else if (done.load(std::memory_order_acquire) && r.Size() == 0)
                    break;
            }
        });
        for (uint32_t i = 1; i <= kCount; ++i)
            r.Push(i);
        done.store(true, std::memory_order_release);
        consumer.join();
        dropped = r.TakeDropped();
        Check(increasing && popped + dropped == kCount && popped > 0, "two-thread stress: ordered, nothing lost uncounted");
    }
}

Sample Base(uint32_t vehicle, int32_t tick, uint64_t ms)
{
    Sample s;
    s.vehicle = vehicle;
    s.tick = tick;
    s.nowMs = ms;
    s.spt = 1.0f / 30.0f;
    s.dtA = s.dtB = s.spt;
    s.haveVehicle = true;
    s.facing = ForwardFromYawPitch(0.2f, 0.1f);
    s.craftFwd = s.facing;
    s.pre[0] = s.post[0] = 0.2f;
    s.pre[1] = s.post[1] = 0.1f;
    s.rollField = 0.0f;
    return s;
}

struct Drained
{
    std::vector<Event> all;
    size_t Count(Kind k) const
    {
        size_t n = 0;
        for (const auto& e : all)
            n += e.kind == k;
        return n;
    }
};
template <size_t N>
Drained Drain(SpscRing<Event, N>& ring)
{
    Drained d;
    Event e;
    while (ring.Pop(e))
        d.all.push_back(e);
    return d;
}

void TestProducer()
{
    static SpscRing<Event, 1024> ring;
    Producer<1024> p(0);
    TagFacts tag;
    tag.valid = true;
    tag.window = 1.0f;
    tag.projBit = 1;

    // A stint: 40 ticks of 33 ms, two calls in tick 5 (a second dispatcher caller).
    uint32_t newTicks = 0, needTags = 0;
    for (int i = 0; i < 40; ++i)
    {
        const int calls = i == 5 ? 2 : 1;
        for (int c = 0; c < calls; ++c)
        {
            Sample s = Base(0xE0010002u, 1000 + i, 10000 + 33ull * i);
            const CallStep step = p.Begin(s, 7, ring);
            newTicks += step.newTick;
            if (step.needTag)
            {
                ++needTags;
                p.SetTag(tag);
            }
            p.Finish(s, step, ring);
        }
    }
    Drained d = Drain(ring);
    Check(newTicks == 40, "41 calls over 40 ticks: 40 new ticks (the repeat call is not one)");
    Check(needTags == 1, "the tag is read once per stint");
    Check(d.Count(Kind::StintBegin) == 1 && d.Count(Kind::StintEnd) == 0, "one stint begins, none ends");
    Check(d.Count(Kind::Link) == 30, "the LINK burst covers the stint's first 30 ticks");
    Check(d.Count(Kind::Dt) == 1, "the time arguments are logged once while they do not change");
    Check(d.Count(Kind::Stamp) == 1, "the stamp's starting value is logged once");
    Check(d.Count(Kind::Roll) == 1, "the roll mode's starting value is logged once");
    Check(d.Count(Kind::State) == 0 && d.Count(Kind::Phys) == 0 && d.Count(Kind::Type) == 0,
          "fields the title does not supply log nothing");
    bool linkExact = true;
    for (const auto& e : d.all)
        if (e.kind == Kind::Link)
            linkExact &= Near(e.u.link.err, 0.0f, 1e-3f) && Near(e.u.link.facingYaw, 0.2f) && Near(e.u.link.facingPitch, 0.1f);
    Check(linkExact, "a record that equals the facing reads err 0");
    Check(d.Count(Kind::Window) == 0, "no 2 s window yet at 1.3 s");

    // 2 s later in the same stint (no gap > 500 ms): one window summary.
    for (int i = 40; i < 70; ++i)
    {
        Sample s = Base(0xE0010002u, 1000 + i, 10000 + 33ull * i);
        if (i == 50)
        {
            s.stamp = 1045;   // a forced steer 5 ticks ago, window 1 s: open
            s.stampDir = {0, 1, 0};
        }
        if (i > 50)
            s.stamp = 1045;
        const CallStep step = p.Begin(s, 7, ring);
        p.Finish(s, step, ring);
    }
    d = Drain(ring);
    Check(d.Count(Kind::Window) == 1, "a window summary after 2 s");
    Check(d.Count(Kind::Stamp) == 1, "the stamp change is logged");
    for (const auto& e : d.all)
    {
        if (e.kind == Kind::Stamp)
            Check(e.u.stampEv.stamp == 1045 && e.u.stampEv.was == -1 && e.u.stampEv.open == 1 &&
                      Near(e.u.stampEv.age, 5.0f / 30.0f) && e.u.stampEv.projBit == 1,
                  "the stamp line: value, previous, age, open, the tag's project bit");
        if (e.kind == Kind::Window)
            Check(e.u.window.windowTicks > 0 && e.u.window.rateExcluded + 1 >= e.u.window.windowTicks,
                  "window ticks are counted and excluded from the rate (the open tick closes at the next)");
    }

    // A different vehicle: the stint ends and another begins.
    {
        Sample s = Base(0xE0030004u, 1070, 10000 + 33ull * 70);
        const CallStep step = p.Begin(s, 7, ring);
        Check(step.stintBegan && step.needTag, "a new vehicle begins a new stint, which reads its tag");
        p.SetTag(tag);
        p.Finish(s, step, ring);
        d = Drain(ring);
        bool reason = false;
        for (const auto& e : d.all)
            if (e.kind == Kind::StintEnd)
                reason = e.u.stint.reason == static_cast<uint8_t>(StintEndReason::Vehicle) && e.vehicle == 0xE0010002u &&
                         e.u.stint.ticks == 70;
        Check(reason, "the old stint ends naming the vehicle change, with its tick count");
    }
    // A gap of a second: a new stint.
    {
        Sample s = Base(0xE0030004u, 1071, 10000 + 33ull * 70 + 1000);
        const CallStep step = p.Begin(s, 7, ring);
        Check(step.stintBegan, "no call for more than 500 ms: a new stint");
        p.SetTag(tag);
        p.Finish(s, step, ring);
    }
    // A load (the tick goes backwards): a new stint.
    {
        Sample s = Base(0xE0030004u, 5, 10000 + 33ull * 70 + 1033);
        const CallStep step = p.Begin(s, 7, ring);
        Check(step.stintBegan, "a tick discontinuity: a new stint");
        p.SetTag(tag);
        p.Finish(s, step, ring);
        d = Drain(ring);
        bool reason = false;
        for (const auto& e : d.all)
            reason |= e.kind == Kind::StintEnd && e.u.stint.reason == static_cast<uint8_t>(StintEndReason::Discontinuity);
        Check(reason, "...named as a discontinuity");
    }
    // A fault: observation stops for the stint while its calls keep coming, then resumes with another vehicle.
    {
        p.MarkFaulted(0xE0030004u, 20000, 1, ring);
        Check(p.SkipFaulted(0xE0030004u, 20033) && p.SkipFaulted(0xE0030004u, 20066), "the faulted stint is skipped");
        Check(!p.SkipFaulted(0xE0050006u, 20099), "another vehicle is not");
        d = Drain(ring);
        Check(d.Count(Kind::Fault) == 1 && d.Count(Kind::StintEnd) == 1, "the fault is logged once and ends the stint");
    }
}

void TestProducerHalo4Fields()
{
    static SpscRing<Event, 256> ring;
    Producer<256> p(1);
    TagFacts tag;
    tag.valid = true;
    tag.window = 0.5f;
    tag.tagType = 13;
    for (int i = 0; i < 3; ++i)
    {
        Sample s = Base(0xE0070008u, 50 + i, 30000 + 300ull * i);
        s.physOff = 0x200;
        s.physMode = i < 2 ? 1 : 7;
        s.overrideSet = 0;
        s.overrideType = 0;
        s.haveUnitFacing = true;
        s.unitFacing = s.facing;
        s.gateBit = 0;
        const CallStep step = p.Begin(s, 9, ring);
        if (step.needTag)
            p.SetTag(tag);
        p.Finish(s, step, ring);
    }
    Drained d = Drain(ring);
    Check(d.Count(Kind::Phys) == 2, "the biped physics mode: once per stint and on its change");
    Check(d.Count(Kind::Type) == 1, "the type line: once per stint while the override does not change");
    for (const auto& e : d.all)
        if (e.kind == Kind::Type)
            Check(e.u.type.tagType == 13 && e.u.type.f58 == 0, "the type line carries the tag's type");
}
void TestChangeLinesHeldBack()
{
    // A stamp re-written every tick for a second (a craft scraping a boundary): at most one line per 250 ms,
    // and the latest value still goes out once it settles.
    static SpscRing<Event, 1024> ring;
    Producer<1024> p(0);
    TagFacts tag;
    tag.valid = true;
    tag.window = 1.0f;
    int32_t lastStamp = -1;
    for (int i = 0; i < 70; ++i)
    {
        Sample s = Base(0xE0090009u, 2000 + i, 50000 + 33ull * i);
        if (i >= 1 && i <= 30)
            lastStamp = s.stamp = 2000 + i;
        else
            s.stamp = lastStamp;
        const CallStep step = p.Begin(s, 5, ring);
        if (step.needTag)
            p.SetTag(tag);
        p.Finish(s, step, ring);
    }
    Drained d = Drain(ring);
    std::vector<int32_t> stamps;
    for (const auto& e : d.all)
        if (e.kind == Kind::Stamp)
            stamps.push_back(e.u.stampEv.stamp);
    Check(stamps.size() >= 4 && stamps.size() <= 7, "a stamp changing every tick for 1 s: a handful of lines, not 30");
    Check(!stamps.empty() && stamps.back() == 2030, "the latest value still goes out after the hold-off");
    bool counted = false;
    for (const auto& e : d.all)
        counted |= e.kind == Kind::Window && e.u.window.heldBack > 0;
    Check(counted, "the window summary counts the held-back ticks");
}

void TestStaleStint()
{
    // A flight that stops (the player lands or leaves the craft) inside a 2 s window: the worker ends the stint
    // once the last call is more than 500 ms old, and the last partial window -- with the turn it measured --
    // goes out with it, instead of waiting for the next flight.
    static SpscRing<Event, 1024> ring;
    Producer<1024> p(0);
    TagFacts tag;
    tag.valid = true;
    tag.window = 1.0f;
    const int kTicks = 80;                 // 2.64 s: one full window, then a 0.6 s partial one
    const uint64_t t0 = 60000;
    for (int i = 0; i < kTicks; ++i)
    {
        Sample s = Base(0xE00B000Bu, 3000 + i, t0 + 33ull * i);
        // The first window flushes at tick 61 (2013 ms); a yaw turn of 0.01 rad per tick follows, in the last
        // window only.
        if (i >= 62)
            s.pre[0] = s.post[0] = 0.2f + 0.01f * static_cast<float>(i - 61);
        const CallStep step = p.Begin(s, 4, ring);
        if (step.needTag)
            p.SetTag(tag);
        p.Finish(s, step, ring);
    }
    const uint64_t lastCall = t0 + 33ull * (kTicks - 1);
    Drained d = Drain(ring);
    Check(d.Count(Kind::Window) == 1 && d.Count(Kind::StintEnd) == 0, "one full window, and the stint is still open");
    for (const auto& e : d.all)
        if (e.kind == Kind::Window)
            Check(e.u.window.maxYaw == 0.0f && e.u.window.maxTurn < 0.05f, "the full window saw no turn");

    Check(!p.EndIfStale(lastCall + 500, ring) && p.Active() && ring.Size() == 0,
          "500 ms after the last call the stint is not stale yet");
    Check(!p.EndIfStale(lastCall - 10, ring) && p.Active(), "a clock earlier than the last call ends nothing");
    Check(p.EndIfStale(lastCall + 501, ring) && !p.Active(), "501 ms after it, the worker ends the stint");
    d = Drain(ring);
    Check(d.all.size() == 2 && d.all[0].kind == Kind::Window && d.all[1].kind == Kind::StintEnd,
          "its last window goes out, then its END");
    if (d.all.size() == 2)
    {
        const WindowData& w = d.all[0].u.window;
        const StintData& e = d.all[1].u.stint;
        // 0.01 rad of yaw per 1/30 s tick: 0.3 rad/s of yaw, a little less of turn at pitch 0.1 (x cos 0.1).
        Check(Near(w.maxYaw, 0.3f, 1e-3f) && Near(w.maxTurn, 0.3f * std::cos(0.1f), 1e-3f) && w.rateTicks == 18,
              "the last window keeps the turn rate it measured, over its 18 ticks");
        Check(w.seconds > 0.5f && w.seconds < 0.7f, "and says how long it ran (the 0.6 s since the last full window)");
        Check(e.reason == static_cast<uint8_t>(StintEndReason::Gap) && e.ticks == kTicks &&
                  e.calls == static_cast<uint32_t>(kTicks) && d.all[1].vehicle == 0xE00B000Bu,
              "the END line: the gap reason, the stint's ticks and calls, its vehicle");
    }
    Check(!p.EndIfStale(lastCall + 5000, ring) && ring.Size() == 0, "an ended stint is not ended twice");

    // The next flight begins a fresh stint and does not end the old one again.
    {
        Sample s = Base(0xE00B000Bu, 3000 + kTicks + 200, lastCall + 7000);
        const CallStep step = p.Begin(s, 4, ring);
        Check(step.stintBegan && step.needTag, "the next call begins a new stint");
        p.SetTag(tag);
        p.Finish(s, step, ring);
        d = Drain(ring);
        Check(d.Count(Kind::StintEnd) == 0 && d.Count(Kind::StintBegin) == 1, "with no second END");
    }

    // The install's close: an open stint ends as retired, and an ended one is left alone.
    p.EndStint(StintEndReason::Retired, ring);
    d = Drain(ring);
    bool retired = false;
    for (const auto& e : d.all)
        retired |= e.kind == Kind::StintEnd && e.u.stint.reason == static_cast<uint8_t>(StintEndReason::Retired);
    Check(retired && !p.Active(), "an install ends a stint still open as retired");
    p.EndStint(StintEndReason::Retired, ring);
    Check(ring.Size() == 0, "and pushes nothing when none is open");
}
} // namespace

int main()
{
    TestGeometry();
    TestTickGate();
    TestCallerTable();
    TestRateWindow();
    TestWindow();
    TestChangeWatch();
    TestDispatcherFrame();
    TestOwnerClaim();
    TestRing();
    TestProducer();
    TestProducerHalo4Fields();
    TestChangeLinesHeldBack();
    TestStaleStint();
    std::printf("%s: %u type-13 flight log checks, %u failed\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
