// CallNativeWithSpareSlots (src/dll/native_forward.h): a forward to a title native passes the declared
// arguments unchanged, then zeroed spare slots -- the register arguments a short signature leaves unused, and
// kNativeSpareStackSlots stack slots -- so a native that scribbles past its arguments writes into this call's
// own outgoing area. Each fixture below is declared with the padded signature, so it reads exactly what the
// forward put in those slots.
#include "../src/dll/native_forward.h"

#include <cstdio>
#include <cstring>

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

struct Seen
{
    void* event = nullptr;
    uint32_t target = 0;
    int16_t region = 0, node = 0;
    uint16_t material = 0;
    void* result = nullptr;
    float scale = 0.0f;
    float floats[4]{};
    const void* input = nullptr;
    uint64_t spare[8]{};
    unsigned calls = 0;
} seen;

// The ODST / Halo 3 damage event: six declared arguments, then four stack spares.
__declspec(noinline) void __fastcall DamageFixture(void* event, uint32_t target, int16_t region, int16_t node,
                                                  uint16_t material, void* result, uint64_t s0, uint64_t s1,
                                                  uint64_t s2, uint64_t s3)
{
    seen.event = event;
    seen.target = target;
    seen.region = region;
    seen.node = node;
    seen.material = material;
    seen.result = result;
    const uint64_t spares[]{s0, s1, s2, s3};
    std::memcpy(seen.spare, spares, sizeof(spares));
    ++seen.calls;
}
using DamageFn = void(__fastcall*)(void*, uint32_t, int16_t, int16_t, uint16_t, void*);

// Halo 2's damage event: two declared arguments, two register spares, then four stack spares.
__declspec(noinline) void __fastcall ShortFixture(void* event, uint32_t target, uint64_t r8, uint64_t r9,
                                                 uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3)
{
    seen.event = event;
    seen.target = target;
    const uint64_t spares[]{r8, r9, s0, s1, s2, s3};
    std::memcpy(seen.spare, spares, sizeof(spares));
    ++seen.calls;
}
using ShortFn = void(__fastcall*)(void*, uint32_t);

// A float among the register arguments travels in its XMM register, the spares after it as usual.
__declspec(noinline) uint32_t __fastcall FloatFixture(uint32_t unit, float scale, uint64_t r8, uint64_t r9,
                                                     uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3)
{
    seen.target = unit;
    seen.scale = scale;
    const uint64_t spares[]{r8, r9, s0, s1, s2, s3};
    std::memcpy(seen.spare, spares, sizeof(spares));
    ++seen.calls;
    return unit + 1;
}
using FloatFn = uint32_t(__fastcall*)(uint32_t, float);

// The type-13 pilot controllers (Reach +0x64828, Halo 4 +0xAA524): seven declared arguments -- user, vehicle,
// two float deltas in xmm2/xmm3, then the input pointer and two floats on the stack at entry+0x28/+0x30/+0x38
// -- then four stack spares (entry+0x40..+0x58), 11 slots in all.
__declspec(noinline) void __fastcall ControllerFixture(int user, uint32_t vehicle, float yawD, float pitchD,
                                                      const void* input, float dtA, float dtB, uint64_t s0,
                                                      uint64_t s1, uint64_t s2, uint64_t s3)
{
    seen.target = static_cast<uint32_t>(user) ^ vehicle;
    seen.event = reinterpret_cast<void*>(static_cast<uintptr_t>(vehicle));
    seen.region = static_cast<int16_t>(user);
    seen.floats[0] = yawD;
    seen.floats[1] = pitchD;
    seen.floats[2] = dtA;
    seen.floats[3] = dtB;
    seen.input = input;
    const uint64_t spares[]{s0, s1, s2, s3};
    std::memcpy(seen.spare, spares, sizeof(spares));
    ++seen.calls;
}
using ControllerFn = void(__fastcall*)(int, uint32_t, float, float, const void*, float, float);

void Scribble() { std::memset(seen.spare, 0xCD, sizeof(seen.spare)); }
bool SparesZero(size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (seen.spare[i] != 0)
            return false;
    return true;
}
} // namespace

int main()
{
    int eventStorage = 0, resultStorage = 0;
    const auto damage = reinterpret_cast<DamageFn>(&DamageFixture);
    Scribble();
    CallNativeWithSpareSlots(damage, &eventStorage, 0xE1234u, int16_t(-3), int16_t(7), uint16_t(9), &resultStorage);
    Check(seen.calls == 1 && seen.event == &eventStorage && seen.target == 0xE1234u && seen.region == -3 &&
              seen.node == 7 && seen.material == 9 && seen.result == &resultStorage,
          "six declared arguments arrive unchanged");
    Check(SparesZero(4), "then four zeroed stack slots (entry+0x38 through +0x50)");

    const auto shortFn = reinterpret_cast<ShortFn>(&ShortFixture);
    Scribble();
    CallNativeWithSpareSlots(shortFn, &eventStorage, 42u);
    Check(seen.calls == 2 && seen.event == &eventStorage && seen.target == 42u, "two declared arguments arrive");
    Check(SparesZero(6), "then r8/r9 and four stack slots, all zero");

    const auto floatFn = reinterpret_cast<FloatFn>(&FloatFixture);
    Scribble();
    const uint32_t returned = CallNativeWithSpareSlots(floatFn, 5u, 2.5f);
    Check(seen.calls == 3 && seen.target == 5u && seen.scale == 2.5f && returned == 6u,
          "a float argument and the return value pass through");
    Check(SparesZero(6), "the spares after a float argument are zero too");

    const auto controller = reinterpret_cast<ControllerFn>(&ControllerFixture);
    Scribble();
    // Four distinct floats, so a swap or an alias between any two float positions fails -- above all position 4
    // (xmm3, the pitch delta) and position 7 ([rsp+0x30] at the call, entry+0x38), the pair the controllers read.
    CallNativeWithSpareSlots(controller, 1, 0xE0010002u, -0.75f, 0.125f, &resultStorage, 0.0333f, 3.5e-2f);
    Check(seen.calls == 4 && seen.region == 1 && seen.event == reinterpret_cast<void*>(uintptr_t(0xE0010002u)) &&
              seen.target == (1u ^ 0xE0010002u) && seen.floats[0] == -0.75f && seen.floats[1] == 0.125f &&
              seen.input == &resultStorage && seen.floats[2] == 0.0333f && seen.floats[3] == 3.5e-2f,
          "seven declared arguments (floats in positions 3, 4, 6 and 7) arrive unchanged");
    Check(SparesZero(4), "then four zeroed stack slots (entry+0x40 through +0x58)");

    std::printf("%s: %u native forward checks, %u failed\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
