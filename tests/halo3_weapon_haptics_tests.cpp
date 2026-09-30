#include "../src/common/halo3_weapon_haptics_logic.h"
#include <array>
#include <cassert>
#include <cmath>

static float Linear(const float*, float fraction, float endpoint)
{ return fraction * endpoint; }

int main()
{
    using namespace halo3_haptics;
    std::array<float, kVoiceCapacity> ages{0, 0.1f, 0.9f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
    assert(SelectOldest(ages) == 2);
    assert(IsNativeEmptyDatum(0xFFFFFFFFu));
    assert(!IsNativeEmptyDatum(0xFFC00000u));
    assert(std::fabs(MixBands(2.0f, 0.0f) - 0.65f) < 1e-6f);
    Voice fresh{}; fresh.active = true; fresh.lastUpdateMs = 1000;
    assert(FreshAt(fresh, 1100));
    assert(!FreshAt(fresh, 1101));

    Voice voice{};
    voice.scale = 0.8f;
    voice.active = true;
    voice.authored[0] = 0.2f;
    voice.authored[1] = 1.0f;
    voice.authored[6] = 0.1f;
    voice.authored[7] = 0.5f;
    assert(ValidVoice(voice));
    float low = 0, high = 0;
    assert(Sample(voice, Linear, low, high));
    assert(low == 0.0f && high == 0.0f);
    voice.ageSeconds = 0.05f;
    assert(Sample(voice, Linear, low, high));
    assert(std::fabs(low - 0.2f) < 1e-6f);
    assert(std::fabs(high - 0.4f) < 1e-6f);
    assert(!Expired(voice));
    voice.ageSeconds = 0.2f;
    assert(Expired(voice));
    voice.ageSeconds = 0;
    voice.authored[2] = INFINITY;
    assert(ValidVoice(voice)); // Opaque native mapping bytes are retained verbatim.
    voice.authored[0] = INFINITY;
    assert(!ValidVoice(voice));
}
