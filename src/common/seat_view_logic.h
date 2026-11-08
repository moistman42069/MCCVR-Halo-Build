#pragma once
// A FIRST-PERSON SEAT FOR THE CAMERA DIRECTOR ONLY (Halo 3).
//
// Halo 3 has no first-person vehicle view of its own: every stock player seat carries the seat flag `third person
// camera` (bit 4). Clearing that bit in the LOADED vehicle definition while the player sits in the seat (C20,
// halo3_native_seat_patch.inl, kept behind a dev key) makes the game hide the player's own character and draw
// the first-person arms. But the bit is read in two different kinds of place:
//
//   1. the camera DIRECTOR, which decides what this machine draws. It asks the function at halo3+0x1326E8
//      ("is this unit's seat third person?") from three call sites: +0x212074, +0x2127EF, +0x212DCB;
//   2. the SIMULATION. The unit camera evaluator +0x212198 asks the same function (its call at +0x2121B8) for the
//      firing assist (+0x13BAD0) and the control pass's target acquisition (+0x13B08C), and the seated camera
//      evaluator +0x3555EC reads the flags word itself for 28 callers, the fire routine and the control
//      accumulator among them.
//
// A write to the tag reaches both. In a host-authoritative match it makes this machine predict a shot from
// another line than the host fires; in a lockstep game (co-op campaign) it is two different simulations, and a
// client goes out of step with its host within seconds of mounting a turret.
//
// So the tag is left alone and only the director is answered: the three director call sites are redirected to a
// relay that calls the game's own function and then, for the one occupant whose seat view the mod owns, turns the
// settled "third person" answer into "first person". Everything the simulation reads is untouched, and so is the
// function itself -- a host that hooks it for camera modes of its own keeps its hook, and the relay calls through
// it.
//
// THE FUNCTION'S ANSWER (read from the pinned module): it returns 1 for third person and writes a state through
// its second argument: 2 = settled in the seat, where the answer is the seat's bit 4; 1, 3 and 4 are the entry
// window, a seat the unit is still boarding and a vehicle whose own definition forces third person -- all answered
// 1 whatever bit 4 says. Clearing bit 4 therefore changed the answer only in state 2, and the relay changes it
// only there.
//
// THE OTHER WAY ROUND. A seat whose own data already lacks bit 4 (the campaign Pelican's troop seats, seats a host
// or a map adds) is first person in the game itself: the director hides the occupant's character and draws the
// first-person arms and weapon at its camera. A chase camera of the mod's own moves only the eye, so those arms
// would hang in front of it with no body behind them. While such a camera shows a seat, the relay turns a settled
// "first person" answer, whatever produced it, into "third person" for the one occupant, and the director draws the
// seat as it draws every stock seat. The simulation still reads the seat's own flag, so the game's own camera for that unit
// (the unit camera evaluator +0x212198, the firing assist, the acquisition) is the same as an unmodded peer's. The
// director's chase path +0x2144C0 takes the seat's camera only for a seat flagged 0x15 and the unit's own
// otherwise, and a camera with no tracks takes the globals' default, so a seat that was never meant to be seen
// from outside still has a camera to draw from.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace seat_view
{
// halo3.dll, the pinned retail build (the build every other Halo 3 binding in this mod is pinned to).
inline constexpr uint32_t kHalo3ThirdPersonRva = 0x1326E8;

// A director call site: the bytes from the `lea rdx, [rsp+..]` that stages the state argument through the test of
// the answer, the call's displacement left open. `callOffset` is the E8's place in it.
struct DirectorSite
{
    uint32_t anchorRva;
    uint32_t callOffset;
    const char* pattern;
    const char* what;
};
inline constexpr DirectorSite kHalo3DirectorSites[] = {
    {0x212069, 11, "48 8D 94 24 90 00 00 00 41 8B CE E8 ?? ?? ?? ?? 41 8B D7 48 8B CF 44 8B E0 40",
     "the director's update"},
    {0x2127E8, 7, "48 8D 54 24 50 8B CE E8 ?? ?? ?? ?? 83 BB 68 01 00 00 03 75 3B 83 F8 01 75 0F",
     "the director's camera choice for a player"},
    {0x212DC4, 7, "48 8D 54 24 30 8B CF E8 ?? ?? ?? ?? 83 F8 01 0F B6 DB 0F 44 DD 84 DB 74 2B 8B",
     "the director's first-person test"},
};
inline constexpr size_t kDirectorSiteCount = sizeof(kHalo3DirectorSites) / sizeof(kHalo3DirectorSites[0]);
// The simulation's own call, inside the unit camera evaluator. Named so that nothing ever redirects it.
inline constexpr uint32_t kHalo3EvaluatorCallRva = 0x2121B8;

inline constexpr size_t kCallBytes = 5;
inline constexpr int kThirdPerson = 1, kFirstPerson = 0;
inline constexpr int kStateSettled = 2;

// What a director call is told. `result` and `state` are the game's own; `armedUnit` is the occupant whose seat
// view the mod owns (-1 = none); `chaseUnit` is the occupant of a seat that is first person in its own data while
// a chase camera of the mod's shows it (-1 = none).
inline constexpr int Decide(int result, int state, int32_t unit, int32_t armedUnit, int32_t chaseUnit = -1)
{
    if (state != kStateSettled || unit == -1)
        return result;
    if (result == kThirdPerson && unit == armedUnit)
        return kFirstPerson;
    if (result == kFirstPerson && unit == chaseUnit)
        return kThirdPerson;
    return result;
}

// `call rel32` at `site`: where it goes.
inline bool ReadCall(const uint8_t bytes[kCallBytes], uintptr_t site, uintptr_t& target)
{
    if (bytes[0] != 0xE8)
        return false;
    int32_t displacement = 0;
    std::memcpy(&displacement, bytes + 1, sizeof(displacement));
    target = site + kCallBytes + static_cast<intptr_t>(displacement);
    return true;
}
// ...and the five bytes that make it go to `target`. False when `target` is out of a rel32's reach.
inline bool BuildCall(uintptr_t site, uintptr_t target, uint8_t out[kCallBytes])
{
    const int64_t difference = static_cast<int64_t>(target) - static_cast<int64_t>(site + kCallBytes);
    if (difference < INT32_MIN || difference > INT32_MAX)
        return false;
    const int32_t displacement = static_cast<int32_t>(difference);
    out[0] = 0xE8;
    std::memcpy(out + 1, &displacement, sizeof(displacement));
    return true;
}

// THE RELAY. A director site calls it in place of the game's function, with the function's own arguments
// (ecx = unit, rdx = state pointer). It calls the function at `original`, then `adjust(unit, state, result)` and
// returns what that returns:
//
//     push rbx
//     sub  rsp, 0xB0             ; home area, four spare slots, the state pointer, xmm0-xmm5
//     mov  r10, counter
//     lock inc dword [r10]       ; a thread is inside the relay or below it
//     mov  [rsp+0x40], rdx
//     mov  ebx, ecx
//     mov  rax, original
//     call rax
//     movaps [rsp+0x50..0xA0], xmm0..xmm5
//     mov  r8d, eax
//     mov  ecx, ebx
//     mov  rdx, [rsp+0x40]
//     mov  rax, adjust
//     call rax
//     movaps xmm0..xmm5, [rsp+0x50..0xA0]
//     mov  r10, counter
//     lock dec dword [r10]
//     add  rsp, 0xB0
//     pop  rbx
//     ret
//
// `counter` (a 32-bit count the caller owns) is above zero while any thread is between the two marks: with the
// call sites restored and every other thread's instruction pointer outside the relay, a zero count means no
// thread can return into it, and its memory may be released.
// The game's function is called exactly as its own callers call it. tools/audit_caller_area.py finds no caller
// that keeps a volatile register across the call and no use of the caller's frame past the home area; the four
// spare slots and the saved xmm0-xmm5 (which the C++ `adjust` may use and the game's function may not) are margin
// for a build where that is no longer so. kRelayPrologBytes / kRelayFrameBytes describe the frame for the unwind
// data registered with it (BuildRelayUnwind), so a stack walk through the relay is exact.
inline constexpr size_t kRelayBytes = 164;
inline constexpr uint8_t kRelayPrologBytes = 8;
inline constexpr uint32_t kRelayFrameBytes = 0xB0;

inline size_t BuildRelay(uint8_t* out, size_t capacity, uintptr_t original, uintptr_t adjust, uintptr_t counter)
{
    if (!out || capacity < kRelayBytes)
        return 0;
    uint8_t* p = out;
    const auto emit = [&p](std::initializer_list<uint8_t> bytes) {
        for (const uint8_t b : bytes)
            *p++ = b;
    };
    const auto emit64 = [&p](uint64_t value) {
        std::memcpy(p, &value, sizeof(value));
        p += sizeof(value);
    };
    emit({0x53});                                           // push rbx
    emit({0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00});       // sub rsp, 0xB0
    emit({0x49, 0xBA});                                     // mov r10, counter
    emit64(counter);
    emit({0xF0, 0x41, 0xFF, 0x02});                         // lock inc dword ptr [r10]
    emit({0x48, 0x89, 0x54, 0x24, 0x40});                   // mov [rsp+0x40], rdx
    emit({0x8B, 0xD9});                                     // mov ebx, ecx
    emit({0x48, 0xB8});                                     // mov rax, original
    emit64(original);
    emit({0xFF, 0xD0});                                     // call rax
    emit({0x0F, 0x29, 0x44, 0x24, 0x50});                   // movaps [rsp+0x50], xmm0
    emit({0x0F, 0x29, 0x4C, 0x24, 0x60});                   // movaps [rsp+0x60], xmm1
    emit({0x0F, 0x29, 0x54, 0x24, 0x70});                   // movaps [rsp+0x70], xmm2
    emit({0x0F, 0x29, 0x9C, 0x24, 0x80, 0x00, 0x00, 0x00}); // movaps [rsp+0x80], xmm3
    emit({0x0F, 0x29, 0xA4, 0x24, 0x90, 0x00, 0x00, 0x00}); // movaps [rsp+0x90], xmm4
    emit({0x0F, 0x29, 0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00}); // movaps [rsp+0xA0], xmm5
    emit({0x44, 0x8B, 0xC0});                               // mov r8d, eax
    emit({0x8B, 0xCB});                                     // mov ecx, ebx
    emit({0x48, 0x8B, 0x54, 0x24, 0x40});                   // mov rdx, [rsp+0x40]
    emit({0x48, 0xB8});                                     // mov rax, adjust
    emit64(adjust);
    emit({0xFF, 0xD0});                                     // call rax
    emit({0x0F, 0x28, 0x44, 0x24, 0x50});                   // movaps xmm0, [rsp+0x50]
    emit({0x0F, 0x28, 0x4C, 0x24, 0x60});                   // movaps xmm1, [rsp+0x60]
    emit({0x0F, 0x28, 0x54, 0x24, 0x70});                   // movaps xmm2, [rsp+0x70]
    emit({0x0F, 0x28, 0x9C, 0x24, 0x80, 0x00, 0x00, 0x00}); // movaps xmm3, [rsp+0x80]
    emit({0x0F, 0x28, 0xA4, 0x24, 0x90, 0x00, 0x00, 0x00}); // movaps xmm4, [rsp+0x90]
    emit({0x0F, 0x28, 0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00}); // movaps xmm5, [rsp+0xA0]
    emit({0x49, 0xBA});                                     // mov r10, counter
    emit64(counter);
    emit({0xF0, 0x41, 0xFF, 0x0A});                         // lock dec dword ptr [r10]
    emit({0x48, 0x81, 0xC4, 0xB0, 0x00, 0x00, 0x00});       // add rsp, 0xB0
    emit({0x5B});                                           // pop rbx
    emit({0xC3});                                           // ret
    return static_cast<size_t>(p - out);
}

// The relay's UNWIND_INFO (version 1, no handler): `sub rsp, 0xB0` at prolog offset 8, `push rbx` at offset 1.
inline constexpr size_t kRelayUnwindBytes = 12;
inline size_t BuildRelayUnwind(uint8_t* out, size_t capacity)
{
    if (!out || capacity < kRelayUnwindBytes)
        return 0;
    const uint8_t info[kRelayUnwindBytes] = {
        0x01,                    // version 1, no flags
        kRelayPrologBytes,       // size of prolog
        0x03,                    // count of unwind codes (slots)
        0x00,                    // no frame register
        0x08, 0x01,              // at offset 8: UWOP_ALLOC_LARGE, size / 8 in the next slot
        static_cast<uint8_t>((kRelayFrameBytes / 8) & 0xFF), static_cast<uint8_t>((kRelayFrameBytes / 8) >> 8),
        0x01, 0x30,              // at offset 1: UWOP_PUSH_NONVOL rbx
        0x00, 0x00,              // padding to an even count
    };
    std::memcpy(out, info, sizeof(info));
    return sizeof(info);
}
} // namespace seat_view
