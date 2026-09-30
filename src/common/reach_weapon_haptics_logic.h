#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Pure contracts extracted from Reach's native rumble queue and evaluator.
// HREK evidence: a per-user queue has eight 0x34-byte voices. Each voice is
// tag datum, scale, then a 0x2c-byte source descriptor; age is kept in a
// parallel eight-float array. The referenced rmbl tag has two 0x18-byte motor
// bands, each a duration float plus an opaque 0x14-byte mapping record.
namespace reach_haptics {

inline constexpr size_t kVoiceCapacity = 8;
inline constexpr size_t kBands = 2;
inline constexpr size_t kTagBandBytes = 0x18;
inline constexpr size_t kTagCurveBytes = kBands * kTagBandBytes;
inline constexpr size_t kSourceDescriptorBytes = 0x2c;
inline constexpr size_t kSourceAttenuationOffset = 0x28;
inline constexpr size_t kNativeUserRowBytes = 0x1d8;
inline constexpr size_t kNativeAgeOffset = 0x1b0;

struct Source {
    uint32_t owner{UINT32_MAX};
    uint32_t weapon{UINT32_MAX};
    uint32_t generation{};
    uint8_t slot{0xff};
    bool valid{};
};

struct Voice {
    uint32_t tagDatum{UINT32_MAX};
    float scale{};
    std::array<uint8_t, kSourceDescriptorBytes> descriptor{};
    std::array<uint8_t, kTagCurveBytes> tagCurve{};
    float ageSeconds{};
    uint32_t owner{UINT32_MAX};
    uint32_t weapon{UINT32_MAX};
    uint32_t generation{};
    uint8_t slot{0xff};
    uint64_t sourceToken{};
    bool active{};
};

// DAEFC chooses and writes a native slot first. Divert only when exactly one
// slot changed to the requested complete tuple; this preserves native eviction
// and leaves identical/ambiguous traffic untouched.
inline int FindNewNativeVoice(const uint8_t* before, const uint8_t* after,
    uint32_t tagDatum, float scale, const uint8_t* descriptor) noexcept {
    if (!before || !after || !descriptor || tagDatum == UINT32_MAX ||
        !std::isfinite(scale)) return -1;
    int found = -1;
    for (int slot = 0; slot < static_cast<int>(kVoiceCapacity); ++slot) {
        const size_t record = size_t(slot) * 0x34;
        const size_t age = kNativeAgeOffset + size_t(slot) * sizeof(float);
        uint32_t tag{}; float observedScale{}, oldAge{}, newAge{};
        std::memcpy(&tag, after + record, sizeof(tag));
        std::memcpy(&observedScale, after + record + 4, sizeof(observedScale));
        std::memcpy(&oldAge, before + age, sizeof(oldAge));
        std::memcpy(&newAge, after + age, sizeof(newAge));
        if (tag != tagDatum || newAge != 0.0f ||
            std::memcmp(&observedScale, &scale, sizeof(scale)) != 0 ||
            std::memcmp(after + record + 8, descriptor, kSourceDescriptorBytes) != 0)
            continue;
        const bool recordChanged = std::memcmp(before + record, after + record, 0x34) != 0;
        const bool ageReset = std::isfinite(oldAge) && std::isfinite(newAge) &&
            oldAge > 0.0f && newAge == 0.0f;
        if (!recordChanged && !ageReset) continue;
        if (found >= 0) return -1;
        found = slot;
    }
    return found;
}

using CurveFunction = float (*)(const void* nativeMapping, float normalizedAge,
    float endpoint);

inline float Duration(const Voice& voice, size_t band) noexcept {
    float value{};
    if (band >= kBands) return NAN;
    std::memcpy(&value, voice.tagCurve.data() + band * kTagBandBytes,
        sizeof(value));
    return value;
}

inline bool SameSource(const Source& a, const Source& b) noexcept {
    return a.valid && b.valid && a.owner == b.owner && a.weapon == b.weapon &&
        a.generation == b.generation && a.slot == b.slot;
}

// The queue's 0x34-byte slot is tag(4)+scale(4)+descriptor(0x2c). The
// descriptor is opaque except for float at +0x28, which native update writes
// as the spatial attenuation multiplier; preserve sentinel/object fields raw.
inline bool Capture(uint32_t outputUser, const Source& firingSource,
    const Source& currentSource, uint32_t tagDatum, float scale,
    const uint8_t* descriptor, uint64_t sourceToken, Voice& out) noexcept {
    if (outputUser != 0 || !SameSource(firingSource, currentSource) ||
        tagDatum == UINT32_MAX || !descriptor || !sourceToken ||
        !std::isfinite(scale) || scale < 0 || firingSource.slot > 1 ||
        firingSource.generation == 0)
        return false;

    Voice candidate{};
    candidate.tagDatum = tagDatum;
    candidate.scale = scale;
    std::memcpy(candidate.descriptor.data(), descriptor,
        candidate.descriptor.size());
    float attenuation{};
    std::memcpy(&attenuation,
        candidate.descriptor.data() + kSourceAttenuationOffset,
        sizeof(attenuation));
    if (!std::isfinite(attenuation)) return false;
    candidate.owner = firingSource.owner;
    candidate.weapon = firingSource.weapon;
    candidate.generation = firingSource.generation;
    candidate.slot = firingSource.slot;
    candidate.sourceToken = sourceToken;
    candidate.active = true;
    out = candidate;
    return true;
}

inline bool Valid(const Voice& voice) noexcept {
    if (!voice.active || voice.tagDatum == UINT32_MAX ||
        !std::isfinite(voice.scale) || voice.scale < 0 ||
        !std::isfinite(voice.ageSeconds) || voice.ageSeconds < 0 ||
        voice.generation == 0 || voice.slot > 1 || voice.sourceToken == 0)
        return false;
    float attenuation{};
    std::memcpy(&attenuation,
        voice.descriptor.data() + kSourceAttenuationOffset,
        sizeof(attenuation));
    if (!std::isfinite(attenuation)) return false;
    for (size_t band = 0; band < kBands; ++band) {
        const float duration = Duration(voice, band);
        if (!std::isfinite(duration) || duration < 0) return false;
    }
    return true;
}

inline bool Expired(const Voice& voice) noexcept {
    if (!Valid(voice)) return true;
    for (size_t band = 0; band < kBands; ++band) {
        const float duration = Duration(voice, band);
        if (duration > 0 && voice.ageSeconds < duration) return false;
    }
    return true;
}

inline bool Sample(const Voice& voice, CurveFunction curve,
    float& low, float& high) noexcept {
    low = high = 0;
    if (!Valid(voice) || !curve) return false;
    float* outputs[kBands]{&low, &high};
    float attenuation{};
    std::memcpy(&attenuation,
        voice.descriptor.data() + kSourceAttenuationOffset,
        sizeof(attenuation));
    for (size_t band = 0; band < kBands; ++band) {
        const uint8_t* authored = voice.tagCurve.data() + band * kTagBandBytes;
        const float duration = Duration(voice, band);
        if (duration <= 0 || voice.ageSeconds >= duration) continue;
        const float fraction = std::clamp(voice.ageSeconds / duration, 0.0f, 1.0f);
        const float value = curve(authored + sizeof(float), fraction, 1.0f) *
            attenuation * voice.scale;
        if (!std::isfinite(value) || value < 0) {
            low = high = 0;
            return false;
        }
        *outputs[band] = std::clamp(value, 0.0f, 1.0f);
    }
    return true;
}

inline bool Advance(Voice& voice, float deltaSeconds) noexcept {
    if (!Valid(voice) || !std::isfinite(deltaSeconds) || deltaSeconds < 0 ||
        deltaSeconds > 1.0f)
        return false;
    voice.ageSeconds += deltaSeconds;
    if (!std::isfinite(voice.ageSeconds)) return false;
    if (Expired(voice)) voice.active = false;
    return true;
}

} // namespace reach_haptics
