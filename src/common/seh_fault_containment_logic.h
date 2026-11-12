#pragma once

#include <cstddef>
#include <cstdint>

// Which structured exceptions a mod-owned __except may swallow.
//
// A handler that catches a fault unwinds every frame between the fault and
// itself. When any of those frames belongs to the GAME, the game's own work is
// abandoned half-done: in build bd2aac6 a Halo 2 Anniversary renderer null
// dereference (halo2 +0x107449, then +0x3D0730) was caught by the mod's
// EXCEPTION_EXECUTE_HANDLER around the scene render. The unwind skipped the
// renderer's own lock/event cleanup, the render thread never presented again
// and the game froze silently instead of crashing with a dump.
//
// So a mod handler contains a fault only when it is provably the mod's own:
// the faulting instruction AND every frame the unwind would discard lie inside
// HaloMCCVR.dll's image. Anything else propagates to the game's / the OS's own
// handling. Pure logic, no Windows types, so it is unit-tested offline.

struct FaultImageRange
{
    uintptr_t base = 0;
    size_t size = 0;
};

constexpr bool FaultImageContains(const FaultImageRange& image, uintptr_t address)
{
    return image.base != 0 && image.size != 0 && address >= image.base &&
        address - image.base < image.size;
}

enum class FaultContainment : uint8_t
{
    Contain = 0,     // EXCEPTION_EXECUTE_HANDLER
    Propagate,       // EXCEPTION_CONTINUE_SEARCH
};

// faultAddress: the faulting instruction (ExceptionAddress / Rip).
// frames: the return address of each frame the handler's unwind would discard,
// innermost first, as far as the stack walk got (it stops at the first foreign
// frame, so the last entry may be that frame).
// walkReachedHandler: the walk reached the frame that owns the handler, i.e.
// `frames` is the complete list. An incomplete walk proves nothing and
// propagates.
constexpr FaultContainment DecideFaultContainment(
    const FaultImageRange& self, uintptr_t faultAddress,
    const uintptr_t* frames, size_t frameCount, bool walkReachedHandler)
{
    if (!FaultImageContains(self, faultAddress))
        return FaultContainment::Propagate;
    for (size_t i = 0; i < frameCount; ++i)
    {
        if (!frames || !FaultImageContains(self, frames[i]))
            return FaultContainment::Propagate;
    }
    return walkReachedHandler ? FaultContainment::Contain
                              : FaultContainment::Propagate;
}

// The first address that made the decision Propagate (the fault itself when it
// is foreign, else the first foreign frame), or 0 when there is none (contained
// or an incomplete walk through mod frames only).
constexpr uintptr_t FirstForeignFaultAddress(
    const FaultImageRange& self, uintptr_t faultAddress,
    const uintptr_t* frames, size_t frameCount)
{
    if (!FaultImageContains(self, faultAddress))
        return faultAddress;
    for (size_t i = 0; frames && i < frameCount; ++i)
    {
        if (!FaultImageContains(self, frames[i]))
            return frames[i];
    }
    return 0;
}
