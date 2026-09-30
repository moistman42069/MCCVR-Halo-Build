#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace halo3_haptics {
inline constexpr size_t kBands = 2;
inline constexpr size_t kCurveFloatsPerBand = 6;
inline constexpr size_t kVoiceCurveBytes = kBands * kCurveFloatsPerBand * sizeof(float);
inline constexpr size_t kVoiceCapacity = 8;

struct Voice {
    std::array<float, kBands * kCurveFloatsPerBand> authored{};
    float scale{};
    float ageSeconds{};
    unsigned owner{};
    unsigned weapon{};
    unsigned generation{};
    unsigned slot{};
    uint64_t sourceToken{};
    uint64_t lastUpdateMs{};
    bool active{};
};

using CurveFunction = float (*)(const float* authoredCurve, float fraction,
    float endpoint);

inline bool ValidVoice(const Voice& voice) noexcept {
    if (!std::isfinite(voice.scale) || voice.scale < 0 ||
        !std::isfinite(voice.ageSeconds) || voice.ageSeconds < 0)
        return false;
    for (size_t band = 0; band < kBands; ++band) {
        const float duration = voice.authored[band * kCurveFloatsPerBand];
        if (!std::isfinite(duration) || duration < 0) return false;
    }
    return true;
}

inline bool Expired(const Voice& voice) noexcept {
    if (!voice.active || !ValidVoice(voice)) return true;
    bool anyActiveBand = false;
    for (size_t band = 0; band < kBands; ++band) {
        const float duration = voice.authored[band * kCurveFloatsPerBand];
        if (duration > 0 && voice.ageSeconds < duration) anyActiveBand = true;
    }
    return !anyActiveBand;
}

inline bool FreshAt(const Voice& voice, uint64_t currentTimeMs,
    uint64_t maximumAgeMs = 100) noexcept {
    return voice.active && voice.lastUpdateMs != 0 && currentTimeMs >= voice.lastUpdateMs &&
        currentTimeMs - voice.lastUpdateMs <= maximumAgeMs;
}

inline float MixBands(float low, float high) noexcept {
    if (!std::isfinite(low) || !std::isfinite(high) || low < 0 || high < 0) return 0;
    return 0.65f * std::clamp(low, 0.0f, 1.0f) +
        0.35f * std::clamp(high, 0.0f, 1.0f);
}

inline bool Sample(const Voice& voice, CurveFunction curve, float& low,
    float& high) noexcept {
    low = high = 0;
    if (!curve || !ValidVoice(voice)) return false;
    float* outputs[kBands]{&low, &high};
    for (size_t band = 0; band < kBands; ++band) {
        const float* authored = voice.authored.data() + band * kCurveFloatsPerBand;
        const float duration = authored[0];
        if (duration <= 0 || voice.ageSeconds >= duration) continue;
        const float fraction = std::clamp(voice.ageSeconds / duration, 0.0f, 1.0f);
        const float value = curve(authored + 1, fraction, 1.0f) * voice.scale;
        if (!std::isfinite(value) || value < 0) return false;
        *outputs[band] = value;
    }
    return true;
}

inline size_t SelectOldest(const std::array<float, kVoiceCapacity>& ages) noexcept {
    size_t selected = 0;
    float maximum = ages[0];
    for (size_t index = 1; index < ages.size(); ++index) {
        if (ages[index] > maximum) {
            maximum = ages[index];
            selected = index;
        }
    }
    return selected;
}

inline constexpr uint32_t kNativeEmptyDatum = UINT32_MAX;
inline bool IsNativeEmptyDatum(uint32_t datum) noexcept {
    return datum == kNativeEmptyDatum;
}

} // namespace halo3_haptics
