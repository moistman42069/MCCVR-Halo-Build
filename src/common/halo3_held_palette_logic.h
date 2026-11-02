#pragma once
#include "fp_source_ownership.h"
#include <span>

// Admission is supplied by Halo 3's existing native body/held-model remap
// classifier. The camera and optional unmapped outputs never become gun parts.
inline bool ResolveHalo3HeldPaletteSources(
    bool nativeHeldModel, std::span<const int32_t> remap,
    int sourceCount, int cameraControl, FpHeldSources& out) noexcept
{
    out = {};
    if (!nativeHeldModel || sourceCount <= 0 || sourceCount > 64 ||
        cameraControl < 0 || cameraControl >= sourceCount ||
        remap.empty() || remap.size() > 64)
        return false;
    FpHeldSources candidate{};
    candidate.explicitMap = true;
    for (int source : remap)
    {
        if (source == -1) continue;
        if (source < 0 || source >= sourceCount || source == cameraControl)
            return false;
        const uint64_t bit = uint64_t{1} << source;
        if (candidate.bits[0] & bit) return false;
        candidate.bits[0] |= bit;
    }
    if (!candidate.bits[0]) return false;
    out = candidate;
    return true;
}

inline bool Halo3FpSourceVisible(int source, int count, uint64_t hands,
                                const FpHeldSources& held) noexcept
{
    return source >= 0 && source < count && source < 64 &&
        ((hands & (uint64_t{1} << source)) ||
         FpSourceIndexIsHeldObject(source,count,-1,held));
}
