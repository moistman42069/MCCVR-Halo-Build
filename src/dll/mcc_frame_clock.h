#pragma once

// MCC's frame clock in Halo Reach and Halo 4, read at runtime (dev measurement only). The layout -- which
// TLS block slots hold game_time_globals and the frame interpolator's state, where the tick counter and the blend factor
// sit -- is DECODED from the engine's own instructions (as_seen_logic.h section 8: the fraction leaf, the factor writer
// and the tick increment, each signature matching exactly once in its module), never assumed. Shared by the VR dev
// witnesses (game.cpp) and the flat probe (src/probe/flat_probe.cpp).
//
// The two pointers live in the engine's per-thread TLS block, which only the threads that run game code fill in: a
// game thread (a tick hook, the fire, the frame's own render) CAPTURES them, and any thread then reads the two values
// through the captured pointers under SEH. A pointer that changes (a new map) is simply captured again.
// ADDR-HYGIENE: dev-only (a measurement; it decides nothing and writes nothing).

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "../common/as_seen_logic.h"
#include "sigscan.h"

namespace mcc_clock
{
struct Signatures
{
    const char* leaf;        // the tick fraction leaf (clamp(game_time + 0x14, 0, 1))
    const char* writer;      // the frame function's call of it and its store into the interpolator state
    const char* increment;   // the end-of-tick counter increment
    const char* secondsPerTick;   // the tick loop's left-over time (optional: its seconds-per-tick offset)
};
// haloreach.dll: 0x5C430, 0x344A5, 0x57A5A, 0x57568 (pinned retail; each unique).
inline constexpr Signatures kReach{
    "8B 0D ?? ?? ?? ?? 0F 57 E4 65 48 8B 04 25 58 00 00 00 BA A0 00 00 00 48 8B 04 C8 48 8B 0C 10 F3 0F 10 59 14 0F 2F "
    "DC",
    "E8 ?? ?? ?? ?? B8 70 00 00 00 4A 8B 04 20 F3 0F 11 40 04",
    "B8 A0 00 00 00 48 8B 04 18 FF 40 0C",
    "B8 A0 00 00 00 66 0F 6E ?? 48 8B 04 30 0F 5B C0 F3 0F 59 40 08"};
// halo4.dll: 0xA0458, 0x61172, 0x9ADBF, 0x9A4DD.
inline constexpr Signatures kHalo4{
    "8B 0D ?? ?? ?? ?? 0F 57 E4 65 48 8B 04 25 58 00 00 00 BA 90 00 00 00 48 8B 04 C8 48 8B 0C 10 F3 0F 10 59 14 0F 2F "
    "DC",
    "E8 ?? ?? ?? ?? B8 78 00 00 00 4A 8B 04 30 F3 0F 11 40 04",
    "B8 90 00 00 00 48 8B 0C 38 44 01 79 0C",
    "B8 90 00 00 00 66 0F 6E ?? 48 8B 04 30 0F 5B C0 F3 0F 59 40 08"};

inline uintptr_t FindUnique(uintptr_t base, size_t size, const char* pattern)
{
    const uintptr_t hit = sig::Find(base, size, pattern);
    if (!hit || sig::Find(hit + 1, base + size - hit - 1, pattern))
        return 0;
    return hit;
}

inline bool CopyCode(uintptr_t at, uint8_t* out, size_t size)
{
    __try
    {
        std::memcpy(out, reinterpret_cast<const void*>(at), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

struct Clock
{
    as_seen::FrameClockLayout layout{};
    uintptr_t base = 0;
    std::atomic<uintptr_t> gameTime{0}, interp{0};
    std::atomic<uint32_t> captures{0};

    void Reset()
    {
        layout = as_seen::FrameClockLayout{};
        base = 0;
        gameTime.store(0, std::memory_order_release);
        interp.store(0, std::memory_order_release);
        captures.store(0, std::memory_order_relaxed);
    }

    // Every signature exactly once in [moduleBase, +size), then the decode. False (and the clock off) otherwise; `why`
    // names the first anchor that did not prove.
    bool Resolve(uintptr_t moduleBase, size_t size, const Signatures& s, const char** why = nullptr)
    {
        Reset();
        const uintptr_t leaf = FindUnique(moduleBase, size, s.leaf);
        const uintptr_t writer = FindUnique(moduleBase, size, s.writer);
        const uintptr_t increment = FindUnique(moduleBase, size, s.increment);
        const char* failed = !leaf ? "the tick fraction leaf" : !writer ? "the factor writer"
                           : !increment ? "the tick increment" : nullptr;
        uint8_t leafBytes[40]{}, writerBytes[19]{}, incrementBytes[13]{};
        if (!failed && (!CopyCode(leaf, leafBytes, sizeof(leafBytes)) ||
                        !CopyCode(writer, writerBytes, sizeof(writerBytes)) ||
                        !CopyCode(increment, incrementBytes, sizeof(incrementBytes))))
            failed = "a code read";
        if (!failed &&
            !as_seen::DecodeFrameClock(leafBytes, sizeof(leafBytes), static_cast<uint32_t>(leaf - moduleBase),
                                       writerBytes, sizeof(writerBytes), static_cast<uint32_t>(writer - moduleBase),
                                       incrementBytes, sizeof(incrementBytes), layout))
            failed = "the decode";
        if (!failed && (layout.tlsIndexRva >= size || layout.tlsIndexRva < 0x1000))
            failed = "the TLS index's place";
        if (why)
            *why = failed;
        if (failed)
        {
            layout = as_seen::FrameClockLayout{};
            return false;
        }
        base = moduleBase;
        // Optional: the game's own tick length (unread without it; the hosts fall back to a measured tick).
        if (const uintptr_t left = s.secondsPerTick ? FindUnique(moduleBase, size, s.secondsPerTick) : 0)
        {
            uint8_t leftBytes[21]{};
            if (CopyCode(left, leftBytes, sizeof(leftBytes)))
                (void)as_seen::DecodeSecondsPerTick(leftBytes, sizeof(leftBytes), layout);
        }
        return true;
    }

    // On a game thread: take this thread's game_time and interpolator pointers when its TLS block has both.
    bool Capture()
    {
        if (!layout.valid || !base)
            return false;
        __try
        {
            const uint32_t index = *reinterpret_cast<const uint32_t*>(base + layout.tlsIndexRva);
            const auto* slots = reinterpret_cast<const uintptr_t*>(__readgsqword(0x58));
            if (!slots || index > 0x400)
                return false;
            const uintptr_t block = slots[index];
            if (!block)
                return false;
            const uintptr_t time = *reinterpret_cast<const uintptr_t*>(block + layout.gameTimeSlot);
            const uintptr_t state = *reinterpret_cast<const uintptr_t*>(block + layout.interpSlot);
            if (!time || !state)
                return false;
            if (gameTime.load(std::memory_order_relaxed) != time)
                gameTime.store(time, std::memory_order_release);
            if (interp.load(std::memory_order_relaxed) != state)
                interp.store(state, std::memory_order_release);
            captures.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Any thread: the tick counter (-1 unknown) and the blend factor (NaN unknown or out of [0, 1]); `tickMs` (optional)
    // the game's seconds per tick in ms (NaN unknown).
    void Read(int64_t& tick, float& factor, float* tickMs = nullptr) const
    {
        tick = -1;
        factor = NAN;
        if (tickMs)
            *tickMs = NAN;
        const uintptr_t time = gameTime.load(std::memory_order_acquire);
        const uintptr_t state = interp.load(std::memory_order_acquire);
        __try
        {
            if (time)
            {
                int32_t value = 0;
                std::memcpy(&value, reinterpret_cast<const void*>(time + layout.tickOffset), sizeof(value));
                if (value >= 0)
                    tick = value;
                if (tickMs && layout.secondsPerTickOffset)
                {
                    float seconds = NAN;
                    std::memcpy(&seconds, reinterpret_cast<const void*>(time + layout.secondsPerTickOffset),
                                sizeof(seconds));
                    if (std::isfinite(seconds) && seconds > 0.001f && seconds < 0.2f)
                        *tickMs = seconds * 1000.0f;
                }
            }
            if (state)
            {
                float f = NAN;
                std::memcpy(&f, reinterpret_cast<const void*>(state + layout.factorOffset), sizeof(f));
                if (std::isfinite(f) && f >= 0.0f && f <= 1.0f)
                    factor = f;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            tick = -1;
            factor = NAN;
            if (tickMs)
                *tickMs = NAN;
        }
    }
};
} // namespace mcc_clock
