#pragma once
#include <array>
#include <cstdint>

// An explicit selection belongs to one native render-model palette, not to
// the complement of the rendered body. Extra animation/IK nodes stay unowned.
struct FpHeldSources
{
    bool explicitMap = false;
    std::array<uint64_t, 2> bits{};
    constexpr bool operator==(const FpHeldSources&) const noexcept = default;
};

inline constexpr bool FpSourceIndexIsHeldObject(
    int index, int count, int heldObjectStart, const FpHeldSources& sources) noexcept
{
    if (index < 0 || index >= count)
        return false;
    if (sources.explicitMap)
        return index < 128 && (sources.bits[index / 64] & (uint64_t{1} << (index % 64)));
    // Other titles retain their original appended-range behavior.
    return heldObjectStart >= 0 && index >= heldObjectStart;
}
