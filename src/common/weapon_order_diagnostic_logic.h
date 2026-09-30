#pragma once
#include <cstdint>

// Weapon-order diagnostic tranche (read-only evidence).
// Pure admission decision shared by game.cpp's Capture-probe gate and the
// unit tests. No engine state, no gameplay meaning.
namespace weapon_order_diagnostic
{
inline constexpr uint8_t kProbePermissionAllowed = 0;
inline constexpr uint8_t kProbePermissionNoSafeThread = 1;
inline constexpr uint8_t kProbePermissionThreadMismatch = 2;
inline constexpr uint8_t kProbePermissionGuardRejected = 3;

// storedPacked = (title generation << 32) | thread id from the FP-thread
// observation. The observation is only usable inside the recording session
// that produced it: a numeric thread id reused by a later session is not
// authorization until that session observes its own FP entry. A stored
// observation from another session therefore reports NoSafeThread.
inline uint8_t CaptureProbePermission(bool titleMatches,
    bool generationMatches, uint64_t storedPacked, uint64_t storedSession,
    uint64_t currentSession, uint32_t currentThread) noexcept
{
    if (!storedPacked || !storedSession || !currentSession ||
        storedSession != currentSession)
        return kProbePermissionNoSafeThread;
    if (!titleMatches || !generationMatches)
        return kProbePermissionGuardRejected;
    return uint32_t(storedPacked) == currentThread
        ? kProbePermissionAllowed
        : kProbePermissionThreadMismatch;
}
}
