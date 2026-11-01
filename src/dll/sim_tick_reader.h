#pragma once
// THE SIMULATION'S IN-TICK BYTE (Halo Reach, Halo 4). Each thread's engine TLS block points, at one slot, to the
// frame interpolator's state. The tick loop sets one byte of that state before every tick and the post-tick record
// clears it: while it is set the game's own object readers answer the tick's raw state, and the interpolated state
// otherwise. Read on the simulation's thread it says whether that thread is inside a simulation tick, in the engine's
// own word for it, with no hook of ours around the tick.
//
// ADDR-HYGIENE: resolved (the tick loop's own store, one signature that must match exactly once; the slot and the
// byte are the match's operands, the TLS index is the module's PE TLS directory's). Unresolved, InTick answers -1
// and the caller decides: a lane that must not act inside a tick treats "not known" as inside.
#include "sigscan.h"
#include <windows.h>
#include <intrin.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sim_tick
{
struct Reader
{
    const uint32_t* tlsIndex = nullptr;
    uint32_t slot = 0, byte = 0;
};

// mov r8d, <slot>; inc ecx; mov r8, [r8+rsi]; mov byte ptr [r8+<byte>], 1 -- rsi the thread's engine TLS block.
inline constexpr char kTickBegun[] = "41 B8 ?? ?? ?? ?? FF C1 4D 8B 04 30 41 C6 40 ?? 01";

// `base`, `size`: a loaded title module. False (and `out` empty) unless the store is found exactly once and its
// operands and the module's TLS directory are sane.
inline bool Resolve(uintptr_t base, size_t size, Reader& out)
{
    out = Reader{};
    const auto hit = sig::Find(base, size, kTickBegun);
    if (!hit || sig::Find(hit + 1, base + size - hit - 1, kTickBegun))
        return false;
    uint32_t slot = 0;
    std::memcpy(&slot, reinterpret_cast<const void*>(hit + 2), sizeof(slot));
    const uint8_t byte = *reinterpret_cast<const uint8_t*>(hit + 15);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (!slot || slot > 0x4000 || (slot & 7) || byte > 0x7F || !directory.VirtualAddress ||
        directory.VirtualAddress >= size || size - directory.VirtualAddress < sizeof(IMAGE_TLS_DIRECTORY64))
        return false;
    const auto* tls = reinterpret_cast<const IMAGE_TLS_DIRECTORY64*>(base + directory.VirtualAddress);
    if (tls->AddressOfIndex < base || tls->AddressOfIndex - base >= size ||
        size - (tls->AddressOfIndex - base) < sizeof(uint32_t))
        return false;
    out.slot = slot;
    out.byte = byte;
    out.tlsIndex = reinterpret_cast<const uint32_t*>(tls->AddressOfIndex);
    return true;
}

// Inside a simulation tick as the calling thread's engine TLS block reads (1), outside one (0), or not known (-1).
// Guarded: the block and the state are the engine's own.
__declspec(noinline) inline int InTick(const Reader& reader) noexcept
{
    if (!reader.tlsIndex)
        return -1;
    __try
    {
        const uint32_t index = *reader.tlsIndex;
        const auto* slots = reinterpret_cast<const uintptr_t*>(__readgsqword(0x58));
        if (!slots || index >= 0x400 || !slots[index])
            return -1;
        const auto* state = *reinterpret_cast<const uint8_t* const*>(slots[index] + reader.slot);
        return state ? (state[reader.byte] ? 1 : 0) : -1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // namespace sim_tick
