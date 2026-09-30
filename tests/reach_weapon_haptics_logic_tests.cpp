#include "../src/common/reach_weapon_haptics_logic.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace reach_haptics;

static unsigned checks = 0;
static void Check(bool passed, const char* name) {
    ++checks;
    if (!passed) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}
static bool Near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
static float NativeMapping(const void* mapping, float age, float endpoint) {
    (void)age;
    // Stand-in that reads only a test token; production passes this opaque
    // 0x14-byte record to Reach's proven curve evaluator unchanged.
    uint32_t token{};
    std::memcpy(&token, mapping, sizeof(token));
    return endpoint * static_cast<float>(token) * 0.1f;
}

int main() {
    const Source primary{0x12340001, 0x56780002, 17, 0, true};
    const Source secondary{0x12340001, 0x56780003, 17, 1, true};
    uint8_t descriptor[kSourceDescriptorBytes];
    std::memset(descriptor, 0xff, sizeof(descriptor));
    const float attenuation = 0.5f;
    std::memcpy(descriptor + kSourceAttenuationOffset, &attenuation,
        sizeof(attenuation));

    Voice voice{};
    Check(Capture(0, primary, primary, 0x11223344, 0.8f,
        descriptor, 0xABCDEF01, voice), "capture local primary source");
    Check(voice.active && voice.owner == primary.owner &&
        voice.weapon == primary.weapon && voice.generation == 17 &&
        voice.slot == 0 && voice.sourceToken == 0xABCDEF01,
        "captured identity and cancellation token");
    Check(std::memcmp(voice.descriptor.data(), descriptor, sizeof(descriptor)) == 0,
        "preserve raw descriptor including native UINT32_MAX sentinel bytes");

    std::array<uint8_t, kNativeUserRowBytes> before{};
    std::array<uint8_t, kNativeUserRowBytes> after{};
    std::memset(before.data(), 0xff, before.size());
    after = before;
    std::memcpy(after.data() + 2 * 0x34, &voice.tagDatum, 4);
    std::memcpy(after.data() + 2 * 0x34 + 4, &voice.scale, 4);
    std::memcpy(after.data() + 2 * 0x34 + 8, descriptor, sizeof(descriptor));
    float resetAge = 0.0f;
    std::memcpy(after.data() + kNativeAgeOffset + 2 * sizeof(float), &resetAge, 4);
    Check(FindNewNativeVoice(before.data(), after.data(), voice.tagDatum,
        voice.scale, descriptor) == 2, "identify exact native-written slot after eviction policy runs");
    Check(FindNewNativeVoice(before.data(), before.data(), voice.tagDatum,
        voice.scale, descriptor) == -1, "do not divert unchanged pre-existing identical slot");
    auto ambiguous = after;
    std::memcpy(ambiguous.data() + 3 * 0x34, &voice.tagDatum, 4);
    std::memcpy(ambiguous.data() + 3 * 0x34 + 4, &voice.scale, 4);
    std::memcpy(ambiguous.data() + 3 * 0x34 + 8, descriptor, sizeof(descriptor));
    std::memcpy(ambiguous.data() + kNativeAgeOffset + 3 * sizeof(float), &resetAge, 4);
    Check(FindNewNativeVoice(before.data(), ambiguous.data(), voice.tagDatum,
        voice.scale, descriptor) == -1, "reject ambiguous matching native slots");

    // Failed source/user/token admissions leave an existing destination intact.
    const Voice saved = voice;
    Check(!Capture(1, primary, primary, 0x11223344, 0.8f,
        descriptor, 0xABCDEF02, voice), "reject nonlocal output user");
    Check(!Capture(0, primary, secondary, 0x11223344, 0.8f,
        descriptor, 0xABCDEF02, voice), "reject changed weapon/slot identity");
    Check(!Capture(0, primary, primary, 0x11223344, 0.8f,
        descriptor, 0, voice), "reject missing cancellation token");
    Check(voice.weapon == saved.weapon && voice.sourceToken == saved.sourceToken,
        "failed capture does not overwrite existing voice");

    // Each rmbl band has a duration float and opaque mapping bytes.
    const float lowDuration = 2.0f, highDuration = 1.0f;
    const uint32_t lowMap = 5, highMap = 6;
    std::memcpy(voice.tagCurve.data(), &lowDuration, sizeof(lowDuration));
    std::memcpy(voice.tagCurve.data() + sizeof(float), &lowMap, sizeof(lowMap));
    std::memcpy(voice.tagCurve.data() + kTagBandBytes, &highDuration,
        sizeof(highDuration));
    std::memcpy(voice.tagCurve.data() + kTagBandBytes + sizeof(float), &highMap,
        sizeof(highMap));
    voice.ageSeconds = 1.0f;
    float low = 0, high = 0;
    Check(Sample(voice, NativeMapping, low, high), "sample valid native bands");
    Check(Near(low, 0.2f), "primary band attenuation and scale");
    Check(high == 0, "expired second band contributes zero");

    Voice differentHand = voice;
    differentHand.slot = 1;
    differentHand.sourceToken = 0xABCDEF03;
    differentHand.ageSeconds = 0.0f;
    Check(Sample(differentHand, NativeMapping, low, high), "sample secondary voice");
    Check(Near(low, 0.2f) && Near(high, 0.24f),
        "secondary bands retain independent envelopes");

    // Invalid native floats fail closed; raw descriptor sentinels remain valid.
    differentHand.scale = NAN;
    Check(!Sample(differentHand, NativeMapping, low, high), "reject nonfinite scale");
    differentHand = voice;
    const float badDuration = INFINITY;
    std::memcpy(differentHand.tagCurve.data() + kTagBandBytes,
        &badDuration, sizeof(badDuration));
    Check(!Sample(differentHand, NativeMapping, low, high), "reject nonfinite duration");
    differentHand = voice;
    const float badAttenuation = NAN;
    std::memcpy(differentHand.descriptor.data() + kSourceAttenuationOffset,
        &badAttenuation, sizeof(badAttenuation));
    Check(!Sample(differentHand, NativeMapping, low, high), "reject nonfinite attenuation");

    voice.ageSeconds = 0.0f;
    Check(!Expired(voice), "voice remains active before authored durations");
    Check(Advance(voice, 1.0f) && Advance(voice, 1.0f), "advance native timing");
    Check(!voice.active && Expired(voice), "expire after both bands complete");
    Check(!Advance(voice, NAN), "reject invalid frame delta");
    std::printf("Reach weapon haptics logic checks: %u\n", checks);
    return 0;
}
