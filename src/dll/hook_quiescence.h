#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// Fixed stack capacity shared by production retirement and fixtures. The
// largest current group is Reach's 18-entry retirement set.
inline constexpr size_t kNativeDetourRangeCapacity=18;

// Close feature admission and disable hooked entries before calling. Functions
// without a MinHook entry may pass a null trampoline (e.g. a gated tick body).
// No trampoline may be removed unless this succeeds.
bool WaitForNativeDetourQuiescence(const void* const* functions,
    const void* const* trampolines,size_t count,const std::atomic<uint32_t>& callbacks);
