// The director-only first-person seat (src/common/seat_view_logic.h): the answer rule, the call bytes, and the
// relay itself, built into executable memory and RUN against stand-ins for the game's function and the mod's
// adjust step, with its unwind data registered and a stack walk taken through it.
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../src/common/seat_view_logic.h"

using namespace seat_view;

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Stand-in for the game's function: answers third person with a chosen state, and dirties every volatile
// register it may (the real one uses rax, rcx, rdx, r8-r11).
static int g_fakeResult = kThirdPerson, g_fakeState = kStateSettled, g_fakeCalls = 0, g_fakeUnit = 0;
static int* g_fakeStatePointer = nullptr;
static __declspec(noinline) int FakeThirdPerson(int unit, int* state)
{
    ++g_fakeCalls;
    g_fakeUnit = unit;
    g_fakeStatePointer = state;
    *state = g_fakeState;
    volatile double scribble = 1.5;
    scribble = scribble * 3.0 + unit;
    return g_fakeResult;
}

// Stand-in for the mod's adjust step: records what the relay handed it, walks the stack, and applies the rule.
static int32_t g_armed = -1;
static int g_adjustCalls = 0, g_adjustUnit = 0, g_adjustResult = -1;
static const int* g_adjustState = nullptr;
static void* g_frames[8]{};
static unsigned short g_frameCount = 0;
static volatile LONG g_inside = 0;          // the relay's count of threads inside it
static LONG g_insideDuringAdjust = -1;
// The return addresses above this function, found the way exception dispatch finds them: each frame's function
// entry looked up (dynamic tables included) and unwound with RtlVirtualUnwind.
static __declspec(noinline) void WalkStack()
{
    CONTEXT context{};
    RtlCaptureContext(&context);
    g_frameCount = 0;
    for (int depth = 0; depth < 8 && context.Rip; ++depth)
    {
        DWORD64 imageBase = 0;
        const PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!entry)
            break;   // a frame with no unwind data: the walk cannot go on
        void* handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry, &context, &handlerData, &establisher,
                         nullptr);
        g_frames[g_frameCount++] = reinterpret_cast<void*>(context.Rip);
    }
}
static __declspec(noinline) int FakeAdjust(int unit, const int* state, int result)
{
    ++g_adjustCalls;
    g_adjustUnit = unit;
    g_adjustState = state;
    g_adjustResult = result;
    g_insideDuringAdjust = g_inside;
    WalkStack();
    return Decide(result, state ? *state : -1, unit, g_armed);
}

int main()
{
    // The answer rule: only the armed occupant, only a settled seat, only a third-person answer.
    Check(Decide(kThirdPerson, kStateSettled, 0x1234, 0x1234) == kFirstPerson, "the armed occupant, settled: first person");
    Check(Decide(kThirdPerson, kStateSettled, 0x1234, 0x9999) == kThirdPerson, "another unit keeps the game's answer");
    Check(Decide(kThirdPerson, kStateSettled, 0x1234, -1) == kThirdPerson, "nothing armed keeps the game's answer");
    Check(Decide(kThirdPerson, kStateSettled, -1, -1) == kThirdPerson, "no unit, nothing armed");
    for (const int state : {0, 1, 3, 4, 5})
        Check(Decide(kThirdPerson, state, 0x1234, 0x1234) == kThirdPerson,
              "boarding, the entry window and a forced third-person vehicle keep the game's answer");
    Check(Decide(kFirstPerson, kStateSettled, 0x1234, 0x1234) == kFirstPerson, "a first-person answer is passed on");
    Check(Decide(7, kStateSettled, 0x1234, 0x1234) == 7, "an answer that is neither is passed on untouched");
    // A seat first person in its own data, shown from a chase camera: only that occupant, only settled, third person.
    Check(Decide(kFirstPerson, kStateSettled, 0x1234, -1, 0x1234) == kThirdPerson, "the chase occupant, settled: third person");
    Check(Decide(kFirstPerson, kStateSettled, 0x1234, -1, 0x9999) == kFirstPerson, "another unit keeps the game's first person");
    Check(Decide(kFirstPerson, kStateSettled, 0x1234, -1, -1) == kFirstPerson, "no chase occupant keeps the game's first person");
    Check(Decide(kFirstPerson, kStateSettled, -1, -1, -1) == kFirstPerson, "no unit, nobody armed either way");
    for (const int state : {0, 1, 3, 4, 5})
        Check(Decide(kFirstPerson, state, 0x1234, -1, 0x1234) == kFirstPerson, "outside a settled seat: the game's answer");
    Check(Decide(kThirdPerson, kStateSettled, 0x1234, -1, 0x1234) == kThirdPerson, "a third-person answer stays third person");
    Check(Decide(7, kStateSettled, 0x1234, -1, 0x1234) == 7, "...and one that is neither is passed on untouched");

    // The call's five bytes.
    uint8_t call[kCallBytes]{};
    uintptr_t target = 0;
    Check(BuildCall(0x7FF600212074ull, 0x7FF6001326E8ull, call) && call[0] == 0xE8, "a backward call is built");
    Check(ReadCall(call, 0x7FF600212074ull, target) && target == 0x7FF6001326E8ull, "...and read back");
    const uint8_t stock[kCallBytes] = {0xE8, 0x6F, 0x06, 0xF2, 0xFF};   // the bytes at halo3+0x212074
    Check(ReadCall(stock, 0x212074, target) && target == kHalo3ThirdPersonRva, "the pinned site's bytes call +0x1326E8");
    Check(BuildCall(0x212074, kHalo3ThirdPersonRva, call) && !std::memcmp(call, stock, kCallBytes),
          "...and the same bytes are built for it");
    Check(BuildCall(0x7FF600212074ull, 0x7FF620000000ull, call) && ReadCall(call, 0x7FF600212074ull, target) &&
              target == 0x7FF620000000ull, "a forward call inside reach");
    Check(!BuildCall(0x7FF600212074ull, 0x7FF700212074ull, call), "a target 4 GB away is refused");
    Check(!BuildCall(0x7FF700212074ull, 0x7FF600212074ull, call), "...in either direction");
    const uint8_t jump[kCallBytes] = {0xE9, 0, 0, 0, 0};
    Check(!ReadCall(jump, 0x1000, target), "a jump is not a call");
    for (const DirectorSite& site : kHalo3DirectorSites)
        Check(site.anchorRva + site.callOffset != kHalo3EvaluatorCallRva, "the simulation's own call is not a director site");

    // The relay, run.
    const size_t capacity = 0x1000;
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, capacity, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(memory != nullptr, "executable memory");
    Check(BuildRelay(memory, kRelayBytes - 1, 1, 2, 3) == 0, "too small a buffer is refused");
    const size_t code = BuildRelay(memory, capacity, reinterpret_cast<uintptr_t>(&FakeThirdPerson),
                                   reinterpret_cast<uintptr_t>(&FakeAdjust), reinterpret_cast<uintptr_t>(&g_inside));
    Check(code == kRelayBytes, "the relay has its stated size");
    Check(memory[0] == 0x53 && memory[code - 1] == 0xC3, "it starts with push rbx and ends with ret");
    uint8_t* unwind = memory + 0x100;
    Check(BuildRelayUnwind(unwind, kRelayUnwindBytes) == kRelayUnwindBytes && unwind[1] == kRelayPrologBytes &&
              unwind[6] == kRelayFrameBytes / 8, "the unwind data names the prolog and the frame");
    RUNTIME_FUNCTION function{};
    function.BeginAddress = 0;
    function.EndAddress = static_cast<DWORD>(code);
    function.UnwindData = 0x100;
    Check(RtlAddFunctionTable(&function, 1, reinterpret_cast<DWORD64>(memory)) != 0, "unwind data registered");
    FlushInstructionCache(GetCurrentProcess(), memory, capacity);
    DWORD64 imageBase = 0;
    Check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(memory + 40), &imageBase, nullptr) != nullptr &&
              imageBase == reinterpret_cast<DWORD64>(memory), "...and found for an address inside the relay");

    using RelayFn = int (*)(int, int*);
    const auto relay = reinterpret_cast<RelayFn>(memory);
    int state = -77;

    g_armed = 0x00E30178;
    Check(relay(0x00E30178, &state) == kFirstPerson, "the armed occupant's settled seat is answered first person");
    Check(g_fakeCalls == 1 && g_fakeUnit == 0x00E30178 && g_fakeStatePointer == &state && state == kStateSettled,
          "the game's function got its own arguments and wrote its state");
    Check(g_adjustCalls == 1 && g_adjustUnit == 0x00E30178 && g_adjustState == &state && g_adjustResult == kThirdPerson,
          "the adjust step got the unit, the state pointer and the game's answer");
    Check(g_insideDuringAdjust == 1 && g_inside == 0, "the count is one while a thread is inside, zero after");

    // A stack walk from inside the adjust step: the adjust step, the relay, then main -- which only comes out
    // right if the unwind data describes the relay's frame.
    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto inRelay = [&](void* p) {
        const auto a = reinterpret_cast<uintptr_t>(p);
        return a >= reinterpret_cast<uintptr_t>(memory) && a < reinterpret_cast<uintptr_t>(memory) + code;
    };
    const auto inExe = [&](void* p) {
        const auto a = reinterpret_cast<uintptr_t>(p);
        return a >= exe && a < exe + 0x4000000 && !inRelay(p);
    };
    if (!(g_frameCount >= 3 && inExe(g_frames[0]) && inRelay(g_frames[1]) && inExe(g_frames[2])))
        for (unsigned i = 0; i < g_frameCount; ++i)
            std::fprintf(stderr, "  frame %u: %p (exe %p, relay %p)\n", i, g_frames[i],
                         reinterpret_cast<void*>(exe), static_cast<void*>(memory));
    Check(g_frameCount >= 3 && inExe(g_frames[0]) && inRelay(g_frames[1]) && inExe(g_frames[2]),
          "a stack walk passes through the relay to its caller");

    Check(relay(0x00E30199, &state) == kThirdPerson, "another unit is answered as the game answered");
    g_fakeState = 1;
    Check(relay(0x00E30178, &state) == kThirdPerson && state == 1, "the entry window stays third person");
    g_fakeState = 4;
    Check(relay(0x00E30178, &state) == kThirdPerson, "a vehicle that forces third person stays third person");
    g_fakeState = kStateSettled;
    g_fakeResult = kFirstPerson;
    Check(relay(0x00E30178, &state) == kFirstPerson, "the game's own first person is passed on");
    g_fakeResult = kThirdPerson;
    g_armed = -1;
    Check(relay(0x00E30178, &state) == kThirdPerson, "disarmed: the game's answer");
    Check(g_fakeCalls == 6 && g_adjustCalls == 6, "one call each, every time");

    // Callee-saved registers survive: a loop whose counters live in them across many calls.
    g_armed = 5;
    int firstPerson = 0;
    for (int unit = 0; unit < 1000; ++unit)
        firstPerson += relay(unit, &state) == kFirstPerson ? 1 : 0;
    Check(firstPerson == 1 && g_inside == 0, "a thousand calls, one armed unit, the count back at zero");

    Check(RtlDeleteFunctionTable(&function) != 0, "unwind data removed");
    Check(VirtualFree(memory, 0, MEM_RELEASE) != 0, "memory freed");
    std::printf("PASS: %u seat view checks\n", g_checks);
    return 0;
}
