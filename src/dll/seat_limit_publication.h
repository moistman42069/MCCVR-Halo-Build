#pragma once

// THE SEAT'S STOPS, from the simulation thread to the aim poll (src/common/seat_limit_reticle_logic.h).
//
// The stops live where the seat drive writes: on the simulation thread, inside the title's control-update hook, which
// is the only place the latch can read the game's clamp off the record (and where Halo 2 reads the record's eased
// pitch limits). The reticle is decided on the input thread's aim poll. So the hook publishes, after every seat
// write, the stops it knows this update -- in the record's frame, with that frame -- and the poll reads them back
// (seqlock: one writer, any reader; a torn read retries once and otherwise reads as "none"). An update that does not
// end in a seat write clears the publication, so a stale stop never outlives the write it was read off.

#include "../common/seat_limit_reticle_logic.h"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <limits>

struct SeatStopsPublication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint64_t> atMs{0};
    std::atomic<uint32_t> seatKey{0};
    // bit 0 yaw min, 1 yaw max, 2 pitch min, 3 pitch max, 4 rebased (case b)
    std::atomic<uint32_t> bits{0};
    std::atomic<float> value[2][2]{};   // [axis][0 min, 1 max]
    std::atomic<float> frame[9]{};      // f, l, u
    std::atomic<float> fallbackYaw{0.0f};
    // Optional: the world aim the write was made for (a title whose hand aim is built on the sim thread), else NaN.
    std::atomic<float> targetYaw{0.0f}, targetPitch{0.0f};

    // Sim thread, after a seat write: this update's stops for seat `key` (none is a valid publication: the seat is
    // written and nothing holds it). `recordYaw`: the record's yaw (kept along the record's up, seat_frame).
    void publish(uint32_t key, const seat_limit::Stops& s, float recordYaw, uint64_t nowMs,
                 float worldTargetYaw = std::numeric_limits<float>::quiet_NaN(),
                 float worldTargetPitch = std::numeric_limits<float>::quiet_NaN())
    {
        uint32_t b = 0;
        for (int a = 0; a < 2; ++a)
        {
            if (s.axis[a].hasMin)
                b |= 1u << (a * 2);
            if (s.axis[a].hasMax)
                b |= 1u << (a * 2 + 1);
        }
        if (s.rebased)
            b |= 1u << 4;
        sequence.fetch_add(1, std::memory_order_acq_rel);
        seatKey.store(key, std::memory_order_relaxed);
        bits.store(b, std::memory_order_relaxed);
        for (int a = 0; a < 2; ++a)
        {
            value[a][0].store(s.axis[a].min, std::memory_order_relaxed);
            value[a][1].store(s.axis[a].max, std::memory_order_relaxed);
        }
        for (int i = 0; i < 3; ++i)
        {
            frame[i].store(s.frame.f[i], std::memory_order_relaxed);
            frame[3 + i].store(s.frame.l[i], std::memory_order_relaxed);
            frame[6 + i].store(s.frame.u[i], std::memory_order_relaxed);
        }
        fallbackYaw.store(recordYaw, std::memory_order_relaxed);
        targetYaw.store(worldTargetYaw, std::memory_order_relaxed);
        targetPitch.store(worldTargetPitch, std::memory_order_relaxed);
        atMs.store(nowMs ? nowMs : 1, std::memory_order_relaxed);
        sequence.fetch_add(1, std::memory_order_release);
    }

    // Sim thread: nothing was written this update (or the seat stint ended).
    void clear() { atMs.store(0, std::memory_order_release); }

    // Any thread: the stops published within `maxAgeMs`. False when none is fresh (cleared, stale or torn).
    bool read(uint64_t nowMs, uint64_t maxAgeMs, seat_limit::Stops& out, uint32_t& key, float& recordYaw,
              float* worldTargetYaw = nullptr, float* worldTargetPitch = nullptr) const
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = sequence.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            const uint64_t at = atMs.load(std::memory_order_relaxed);
            seat_limit::Stops s{};
            const uint32_t b = bits.load(std::memory_order_relaxed);
            for (int a = 0; a < 2; ++a)
            {
                s.axis[a].hasMin = (b >> (a * 2)) & 1u;
                s.axis[a].hasMax = (b >> (a * 2 + 1)) & 1u;
                s.axis[a].min = value[a][0].load(std::memory_order_relaxed);
                s.axis[a].max = value[a][1].load(std::memory_order_relaxed);
            }
            s.rebased = (b >> 4) & 1u;
            for (int i = 0; i < 3; ++i)
            {
                s.frame.f[i] = frame[i].load(std::memory_order_relaxed);
                s.frame.l[i] = frame[3 + i].load(std::memory_order_relaxed);
                s.frame.u[i] = frame[6 + i].load(std::memory_order_relaxed);
            }
            const uint32_t k = seatKey.load(std::memory_order_relaxed);
            const float yaw = fallbackYaw.load(std::memory_order_relaxed);
            const float tYaw = targetYaw.load(std::memory_order_relaxed);
            const float tPitch = targetPitch.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence.load(std::memory_order_relaxed) != before)
                continue;
            if (!at || nowMs < at || nowMs - at > maxAgeMs)
                return false;
            out = s;
            key = k;
            recordYaw = yaw;
            if (worldTargetYaw)
                *worldTargetYaw = tYaw;
            if (worldTargetPitch)
                *worldTargetPitch = tPitch;
            return true;
        }
        return false;
    }
};
