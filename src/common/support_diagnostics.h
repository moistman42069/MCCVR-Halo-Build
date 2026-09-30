#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// Hot title/render/input paths only count bounded diagnostic events. The title
// worker formats and writes their messages; no lock, allocation or I/O occurs
// in Record. Repeated events are coalesced between worker polls.
namespace support_diagnostics {
enum class Event : std::size_t { H4PairPresent, H4PairAbsent, H4HeldMismatch, H4OwnerFallback, H4OwnerWeaponsUnavailable, H4OwnerAbsent, H4OwnerCandidateMismatch, H4OwnerGenerationMismatch, H4OwnerResolved, Disabled, AcquisitionEvidenceMissing, Released, Replacement, WeaponAbsent, SwitchInherited, Bound, ReleaseRequired, ReleasedExplicit, ReleasedHandedness, ReleasedGesture, ReleasedLifecycle, ReleasedDisabled, ReleasedAdmission };
inline constexpr const char* messages[] = {
    "Persistent support grip: Halo 4 pair resolved present",
    "Persistent support grip: Halo 4 pair resolved absent",
    "Persistent support grip: Halo 4 held record differs from frozen primary; pair decision retained",
    "Halo 4 owner evidence: using guarded first-person fallback",
    "Halo 4 owner evidence: primary weapon reader unavailable or absent",
    "Halo 4 owner evidence: primary absent",
    "Halo 4 owner evidence: candidate mismatch",
    "Halo 4 owner evidence: generation changed",
    "Halo 4 owner evidence: resolved",
    "Persistent support grip: disabled; relationship released",
    "Persistent support grip: acquisition ignored without coherent owner evidence",
    "Persistent support grip: relationship released",
    "Persistent support grip: replacement owner observed",
    "Persistent support grip: proven weapon absence",
    "Persistent support grip: switched-weapon inheritance rebound owner",
    "Persistent support grip: bound owner",
    "Persistent support grip: acquisition blocked until Grip released",
    "Persistent support grip: relationship released (player release)",
    "Persistent support grip: relationship released (handedness changed)",
    "Persistent support grip: relationship released (weapon interaction)",
    "Persistent support grip: relationship released (title or generation changed)",
    "Persistent support grip: relationship released (two-hand aiming disabled)",
    "Persistent support grip: relationship released (support admission denied)",
};
inline std::atomic<uint32_t> pending[sizeof(messages) / sizeof(messages[0])]{};
static_assert(std::atomic<uint32_t>::is_always_lock_free);
inline void Record(Event value) noexcept {
    const auto event = static_cast<std::size_t>(value);
    if (event < sizeof(messages) / sizeof(messages[0]))
        pending[event].fetch_add(1, std::memory_order_relaxed);
}
template<class Sink> inline void Drain(Sink&& sink) {
    for (std::size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        const auto count = pending[i].exchange(0, std::memory_order_relaxed);
        if (count) sink(messages[i], count);
    }
}
}
