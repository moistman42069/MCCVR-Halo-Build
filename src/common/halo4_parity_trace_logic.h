#pragma once

#include <cstddef>
#include <cstdint>

// Diagnostic-only bookkeeping for Halo 4's retained CUI stream.  The runtime
// owns the atomics; these pure helpers keep the bounded slot/bucket policy
// independently testable and make overflow explicit instead of silently
// merging unrelated command or transform identities.
inline constexpr size_t kHalo4ParityCommandBucketCount = 256;
inline constexpr size_t kHalo4ParityTransformSlotCount = 32;
inline constexpr int32_t kHalo4ParityEmptyTransformId = INT32_MIN;

constexpr bool Halo4ParityCommandFitsBucket(int16_t command) noexcept
{
    return command >= 0 &&
        static_cast<uint16_t>(command) < kHalo4ParityCommandBucketCount;
}

constexpr size_t Halo4ParityCommandBucket(int16_t command) noexcept
{
    return static_cast<size_t>(static_cast<uint16_t>(command));
}

template <size_t N>
constexpr int Halo4ParityFindTransformSlot(
    const int32_t (&ids)[N], int32_t transformId) noexcept
{
    for (size_t i = 0; i < N; ++i)
        if (ids[i] == transformId)
            return static_cast<int>(i);
    for (size_t i = 0; i < N; ++i)
        if (ids[i] == kHalo4ParityEmptyTransformId)
            return static_cast<int>(i);
    return -1;
}

