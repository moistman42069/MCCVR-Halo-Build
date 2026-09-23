#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "runtime_types.h"

namespace subtitles
{
inline constexpr size_t kTextCapacity = 1024;

// Native final-handoff payloads use UTF-16, with |n as an authored line break.
// Unknown control/markup and malformed surrogate pairs stay with native UI.
inline bool Normalize(const wchar_t* source, size_t length,
    wchar_t* output, size_t capacity) noexcept
{
    if (!source || !output || capacity < 2 || !length || length >= kTextCapacity)
        return false;
    size_t start = 0;
    for (size_t i = 1; i + 2 < length; ++i)
    {
        if (source[i] != L'&' || source[i + 1] != L'i' || source[i + 2] != L'd')
            continue;
        if (i > 16 || start) return false;
        for (size_t prefix = 0; prefix < i; ++prefix)
            if (!((source[prefix] >= L'a' && source[prefix] <= L'z') ||
                  (source[prefix] >= L'0' && source[prefix] <= L'9') || source[prefix] == L'_'))
                return false;
        size_t separator = i + 3;
        while (separator < length && source[separator] != 0xE900)
        {
            if (source[separator] < 0x20 || source[separator] > 0x7E ||
                source[separator] == L'&' || source[separator] == L'|') return false;
            ++separator;
        }
        if (separator == i + 3 || separator + 1 >= length) return false;
        // Keep the speaker's authored text in the caption without displaying
        // the engine-only identifier/private-use delimiter.
        start = i + 3;
        break;
    }
    size_t written = 0;
    bool visible = false;
    for (size_t i = start; i < length; ++i)
    {
        wchar_t c = source[i];
        if (c >= 0xE000 && c <= 0xF8FF)
        {
            if (c != 0xE900 || !start || written + 2 >= capacity) return false;
            output[written++] = L':'; output[written++] = L' ';
            start = 0;
            continue;
        }
        if (c == L'|')
        {
            if (++i >= length || source[i] != L'n') return false;
            c = L'\n';
        }
        else if (c >= 0xD800 && c <= 0xDBFF)
        {
            if (++i >= length || source[i] < 0xDC00 || source[i] > 0xDFFF ||
                written + 2 >= capacity) return false;
            output[written++] = c; output[written++] = source[i]; visible = true;
            continue;
        }
        else if ((c >= 0xDC00 && c <= 0xDFFF) ||
            (c < 0x20 && c != L'\n' && c != L'\r' && c != L'\t')) return false;
        if (written + 1 >= capacity) return false;
        output[written++] = c;
        visible |= c > L' ';
    }
    output[written] = 0;
    return visible;
}

inline uint32_t DurationMs(float seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds <= 0.0f || seconds > 600.0f) return 0;
    return static_cast<uint32_t>(std::ceil(static_cast<double>(seconds) * 1000.0));
}

inline bool Current(GameTitle owner, uint32_t generation, uint64_t expires,
    GameTitle active, uint32_t activeGeneration, uint64_t now) noexcept
{
    return owner != GameTitle::None && owner == active && generation != 0 &&
        generation == activeGeneration && expires > now;
}
}
