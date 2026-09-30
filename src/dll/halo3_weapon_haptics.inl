// Halo 3's authored firing recoil is nested in the damage-response worker.
// The worker also performs visual effects, so this adapter retires only its
// exact newly written vibration slot after a private copy was staged.
#include "../common/halo3_haptic_contract.h"
#include "../common/halo3_weapon_haptics_logic.h"
using Halo3WeaponShotFn = void(__fastcall*)(uint32_t, int16_t, uint8_t);
using Halo3WeaponTriggerUpdateFn = uint8_t(__fastcall*)(uint32_t);
using Halo3DamageEffectWrapperFn = void(__fastcall*)(uint32_t, uint32_t, float);
using Halo3PlayerEffectFn = void(__fastcall*)(uint32_t, int32_t, uint32_t,
    uint64_t, uint64_t, uint32_t, uint32_t, uint8_t);
using Halo3EffectWorkerFn = void(__fastcall*)(uint32_t, int32_t, uint32_t,
    uint64_t, uint64_t, uint32_t, float, uint8_t);
using Halo3HapticEvaluatorFn = uint64_t(__fastcall*)(uintptr_t);
using Halo3HapticUpdateFn = void(__fastcall*)(float);
using Halo3HapticCurveFn = float(__fastcall*)(const float*, float, float);

struct Halo3WeaponHapticsRuntime
{
    uintptr_t base{};
    uint32_t generation{};
    void* weaponTarget{};
    void* triggerUpdateTarget{};
    void* wrapperTarget{};
    void* playerEffectTarget{};
    void* workerTarget{};
    void* evaluatorTarget{};
    void* updateTarget{};
    void* weaponOriginal{};
    void* triggerUpdateOriginal{};
    void* wrapperOriginal{};
    void* playerEffectOriginal{};
    void* workerOriginal{};
    void* evaluatorOriginal{};
    void* updateOriginal{};
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> captured{0}, retired{0}, fallback{0}, faults{0};
    std::atomic_flag writer = ATOMIC_FLAG_INIT;
    halo3_haptics::Voice voices[halo3_haptics::kVoiceCapacity]{};
} g_halo3WeaponHaptics;
thread_local bool g_halo3WeaponHapticEvalObserved = false;

struct Halo3WeaponHapticSource
{
    uint32_t owner{UINT32_MAX};
    uint32_t weapon{UINT32_MAX};
    uint32_t generation{};
    bool secondary{};
    bool valid{};
};
thread_local Halo3WeaponHapticSource g_halo3WeaponHapticSource;
thread_local bool g_halo3WeaponHapticFireEffectScope = false;
thread_local bool g_halo3WeaponHapticChargeEffectScope = false;

bool Halo3WeaponHapticIsAdmittedShotCaller(uintptr_t base, uintptr_t caller) noexcept
{
    return caller == base + halo3_haptic_contract::admitted_event_return ||
        caller == base + halo3_haptic_contract::admitted_trigger_return;
}

bool Halo3WeaponHapticIsOwnerEffectCaller(uintptr_t base, uintptr_t caller) noexcept
{
    return caller == base + halo3_haptic_contract::owner_damage_wrapper_return;
}

bool Halo3WeaponHapticIsChargeEffectCaller(uintptr_t base, uintptr_t caller) noexcept
{
    return caller == base + halo3_haptic_contract::charge_effect_return_initial ||
        caller == base + halo3_haptic_contract::charge_effect_return_repeat;
}

bool Halo3WeaponHapticReadEnvelope(uint32_t datum, uint32_t row,
    halo3_haptics::Voice& voice) noexcept
{
    if (datum == UINT32_MAX || row > 0x10000) return false;
    unsigned char* definition = Halo3LoadedTagDefinition(datum);
    if (!definition) return false;
    const int32_t count = *reinterpret_cast<const int32_t*>(definition);
    const uint32_t address = *reinterpret_cast<const uint32_t*>(definition + 4);
    if (count <= 0 || count > 0x10000 || row >= static_cast<uint32_t>(count) || !address)
        return false;
    void** baseSlot = g_halo3TagDataBase;
    auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
    if (!tagBase) return false;
    const auto* nativeRow = tagBase + static_cast<size_t>(address) * 4 +
        static_cast<size_t>(row) * 0xC0;
    std::memcpy(voice.authored.data(), nativeRow + 0x5C,
        halo3_haptics::kVoiceCurveBytes);
    return halo3_haptics::ValidVoice(voice);
}

bool Halo3WeaponHapticQueue(uint32_t user, uint8_t*& queue) noexcept
{
    queue = nullptr;
    if (user >= 4 || !g_engineTlsIndex || *g_engineTlsIndex >= 0x200) return false;
    auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
    if (!slots) return false;
    const auto* tls = static_cast<const uint8_t*>(slots[*g_engineTlsIndex]);
    if (!tls) return false;
    auto* queues = *reinterpret_cast<uint8_t* const*>(tls + 0x2B0);
    if (!queues) return false;
    queue = queues + static_cast<size_t>(user) * 0x98;
    return true;
}

bool Halo3WeaponHapticFireSource(uint32_t weapon,
    Halo3WeaponHapticSource& source) noexcept
{
    source = {};
    if (!g_halo3WeaponHaptics.enabled.load(std::memory_order_acquire) ||
        g_halo3WeaponHaptics.faulted.load(std::memory_order_acquire) ||
        !g_vrAim.load(std::memory_order_acquire) ||
        !g_halo3PlayerUnitGetter || !weapon || weapon == UINT32_MAX)
        return false;
    const uint32_t owner = static_cast<uint32_t>(g_halo3PlayerUnitGetter(0));
    if (owner == UINT32_MAX || weapon == UINT32_MAX) return false;
    uint32_t weapons[2]{};
    if (!Halo3ReadOwnedWeapons(owner, weapons, false)) return false;
    const int slot = ResolveEquippedWeaponSlot(weapon, weapons[0], weapons[1],
        true, weapons[1] != UINT32_MAX);
    if (slot < 0) return false;
    source = {owner, weapon, g_halo3WeaponHaptics.generation, slot == 1, true};
    return source.generation == g_halo3RuntimeGeneration.load(std::memory_order_acquire);
}

__declspec(noinline) void __fastcall Halo3WeaponHapticShotDetour(uint32_t weapon,
    int16_t barrel, uint8_t shotFlags)
{
    auto& feature = g_halo3WeaponHaptics;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const auto previous = g_halo3WeaponHapticSource;
    Halo3WeaponHapticSource captured{};
    volatile bool sourceReadFault = false;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    __try
    {
        __try
        {
            if (Halo3WeaponHapticIsAdmittedShotCaller(feature.base, caller))
                (void)Halo3WeaponHapticFireSource(weapon, captured);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            sourceReadFault = true;
        }
        if (sourceReadFault)
        {
            feature.faulted.store(true, std::memory_order_release);
            ++feature.faults;
        }
        g_halo3WeaponHapticSource = captured;
        // The admitted native shot routine always runs; optional identity reads
        // fail open, while exceptions from the original retain stock semantics.
        if (feature.weaponOriginal)
            reinterpret_cast<Halo3WeaponShotFn>(feature.weaponOriginal)(weapon, barrel, shotFlags);
    }
    __finally
    {
        g_halo3WeaponHapticSource = previous;
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}

__declspec(noinline) uint8_t __fastcall Halo3WeaponHapticTriggerUpdateDetour(uint32_t weapon)
{
    auto& feature = g_halo3WeaponHaptics;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const auto previousSource = g_halo3WeaponHapticSource;
    const bool previousChargeScope = g_halo3WeaponHapticChargeEffectScope;
    Halo3WeaponHapticSource captured{};
    volatile bool sourceReadFault = false;
    __try
    {
        __try
        {
            (void)Halo3WeaponHapticFireSource(weapon, captured);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            sourceReadFault = true;
        }
        if (sourceReadFault)
        {
            feature.faulted.store(true, std::memory_order_release);
            ++feature.faults;
        }
        g_halo3WeaponHapticSource = captured;
        g_halo3WeaponHapticChargeEffectScope = captured.valid;
        uint8_t result = 0;
        if (feature.triggerUpdateOriginal)
            result = reinterpret_cast<Halo3WeaponTriggerUpdateFn>(feature.triggerUpdateOriginal)(weapon);
        return result;
    }
    __finally
    {
        g_halo3WeaponHapticSource = previousSource;
        g_halo3WeaponHapticChargeEffectScope = previousChargeScope;
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}

__declspec(noinline) void __fastcall Halo3WeaponHapticDamageWrapperDetour(
    uint32_t owner, uint32_t datum, float scale)
{
    auto& feature = g_halo3WeaponHaptics;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const bool previous = g_halo3WeaponHapticFireEffectScope;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto source = g_halo3WeaponHapticSource;
    g_halo3WeaponHapticFireEffectScope = source.valid && owner == source.owner &&
        datum != UINT32_MAX && Halo3WeaponHapticIsOwnerEffectCaller(feature.base, caller);
    __try
    {
        if (feature.wrapperOriginal)
            reinterpret_cast<Halo3DamageEffectWrapperFn>(feature.wrapperOriginal)(owner, datum, scale);
    }
    __finally
    {
        g_halo3WeaponHapticFireEffectScope = previous;
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}

__declspec(noinline) void __fastcall Halo3WeaponHapticEffectDetour(uint32_t owner,
    int32_t user, uint32_t datum, uint64_t fourth, uint64_t fifth,
    uint32_t sixth, uint32_t seventh, uint8_t eighth)
{
    auto& feature = g_halo3WeaponHaptics;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const bool previous = g_halo3WeaponHapticFireEffectScope;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    g_halo3WeaponHapticFireEffectScope =
        (previous && caller == feature.base + halo3_haptic_contract::wrapper_player_effect_return) ||
        (g_halo3WeaponHapticChargeEffectScope &&
            Halo3WeaponHapticIsChargeEffectCaller(feature.base, caller));
    __try
    {
        if (feature.playerEffectOriginal)
            reinterpret_cast<Halo3PlayerEffectFn>(feature.playerEffectOriginal)(owner,
                user, datum, fourth, fifth, sixth, seventh, eighth);
    }
    __finally
    {
        g_halo3WeaponHapticFireEffectScope = previous;
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}

bool Halo3WeaponHapticTryCapture(uint32_t owner, int32_t user, uint32_t datum,
    uintptr_t workerReturn, uint8_t*& queue, size_t& selected,
    uint8_t queueSnapshot[0x98],
    halo3_haptics::Voice& pending) noexcept
{
    queue = nullptr;
    const auto source = g_halo3WeaponHapticSource;
    if (!source.valid || !g_halo3WeaponHapticFireEffectScope ||
        owner != source.owner || user != 0 || datum == UINT32_MAX ||
        source.generation != g_halo3WeaponHaptics.generation ||
        (workerReturn != g_halo3WeaponHaptics.base + 0x1B384F &&
         workerReturn != g_halo3WeaponHaptics.base + 0x1B3894) ||
        g_halo3RuntimeGeneration.load(std::memory_order_acquire) != source.generation)
        return false;
    if (!Halo3WeaponHapticQueue(static_cast<uint32_t>(user), queue)) return false;

    float ages[halo3_haptics::kVoiceCapacity]{};
    std::memcpy(ages, queue + 0x70, sizeof(ages));
    for (float age : ages) if (!std::isfinite(age) || age < 0) return false;
    std::memcpy(queueSnapshot, queue, 0x98);
    std::array<float, halo3_haptics::kVoiceCapacity> ageList{};
    std::memcpy(ageList.data(), ages, sizeof(ages));
    selected = halo3_haptics::SelectOldest(ageList);
    pending.owner = source.owner;
    pending.weapon = source.weapon;
    pending.generation = source.generation;
    pending.slot = source.secondary ? 1u : 0u;
    pending.sourceToken = VR_WeaponHapticToken(GameTitle::Halo3,
        source.generation, source.secondary);
    if (!pending.sourceToken) return false;
    pending.lastUpdateMs = GetTickCount64();
    pending.active = true;
    return true;
}

void Halo3WeaponHapticWorkerBody(uint32_t owner, int32_t user, uint32_t datum,
    uint64_t fourth, uint64_t fifth, uint32_t sixth, float scale,
    uint8_t eighth, uintptr_t workerReturn)
{
    auto& feature = g_halo3WeaponHaptics;
    uint8_t* queue = nullptr;
    size_t selected = 0;
    uint8_t queueSnapshot[0x98]{};
    halo3_haptics::Voice pending{};
    bool candidate = false;
    volatile bool captureFault = false;
    if (g_halo3WeaponHapticSource.valid && !feature.faulted.load() &&
            !feature.writer.test_and_set(std::memory_order_acquire))
    {
        __try
        {
            __try
            {
                candidate = Halo3WeaponHapticTryCapture(owner, user, datum,
                    workerReturn, queue, selected, queueSnapshot, pending);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                captureFault = true;
            }
        }
        __finally { feature.writer.clear(std::memory_order_release); }
    }
    if (captureFault)
    {
        feature.faulted.store(true, std::memory_order_release);
        ++feature.faults;
    }

    // Keep the native effect worker outside the optional feature's SEH
    // handler: faults in stock effects must retain their native behavior.
    if (feature.workerOriginal)
        reinterpret_cast<Halo3EffectWorkerFn>(feature.workerOriginal)(owner,
            user, datum, fourth, fifth, sixth, scale, eighth);
    if (candidate && queue && !feature.faulted.load(std::memory_order_acquire))
    {
        volatile bool postFault = false;
        if (feature.writer.test_and_set(std::memory_order_acquire))
            ++feature.fallback;
        else
        {
            __try
            {
                __try
                {
                    uint32_t currentWeapons[2]{};
                    uint8_t* currentQueue = nullptr;
                    const uint32_t currentOwner = g_halo3PlayerUnitGetter
                        ? static_cast<uint32_t>(g_halo3PlayerUnitGetter(0)) : UINT32_MAX;
                    const bool sourceStillCurrent = g_vrAim.load(std::memory_order_acquire) &&
                        pending.generation == g_halo3RuntimeGeneration.load(std::memory_order_acquire) &&
                        currentOwner == pending.owner &&
                        Halo3ReadOwnedWeapons(currentOwner, currentWeapons, false) &&
                        ResolveEquippedWeaponSlot(pending.weapon, currentWeapons[0], currentWeapons[1],
                            true, currentWeapons[1] != UINT32_MAX) == static_cast<int>(pending.slot) &&
                        Halo3WeaponHapticQueue(static_cast<uint32_t>(user), currentQueue) &&
                        currentQueue == queue;
                    if (!sourceStillCurrent)
                    { ++feature.fallback; __leave; }
                    auto* record = queue + selected * 0x0C;
                        const uint32_t newDatum = *reinterpret_cast<const uint32_t*>(record);
                        const uint32_t newRow = *reinterpret_cast<const uint32_t*>(record + 4);
                        const float newScale = *reinterpret_cast<const float*>(record + 8);
                        const float newAge = *reinterpret_cast<const float*>(queue + 0x70 + selected * 4);
                        if (newDatum != datum || !std::isfinite(newScale) ||
                            std::fabs(newScale - scale) > 1e-5f || !std::isfinite(newAge) || newAge != 0.0f)
                        { ++feature.fallback; __leave; }
                    if (std::memcmp(record, queueSnapshot + selected * 0x0C, 12) == 0 &&
                        std::memcmp(queue + 0x70 + selected * 4,
                            queueSnapshot + 0x70 + selected * 4, sizeof(float)) == 0)
                    { ++feature.fallback; __leave; }
                    for (size_t index = 0; index < halo3_haptics::kVoiceCapacity; ++index)
                    {
                        if (index == selected) continue;
                        if (std::memcmp(queue + index * 0x0C,
                                queueSnapshot + index * 0x0C, 12) != 0 ||
                            std::memcmp(queue + 0x70 + index * 4,
                                queueSnapshot + 0x70 + index * 4, sizeof(float)) != 0)
                        { ++feature.fallback; __leave; }
                    }
                        // Copy precisely the response row native selected.
                        if (!Halo3WeaponHapticReadEnvelope(datum, newRow, pending))
                        { ++feature.fallback; __leave; }
                        pending.scale = newScale;
                        pending.ageSeconds = 0;
                        pending.active = true;
                        if (!halo3_haptics::ValidVoice(pending) || halo3_haptics::Expired(pending))
                        { ++feature.fallback; __leave; }
                    const uint64_t currentToken = VR_WeaponHapticToken(GameTitle::Halo3,
                        pending.generation, pending.slot == 1);
                    if (!currentToken || currentToken != pending.sourceToken)
                    { ++feature.fallback; __leave; }
                    size_t freeSlot = halo3_haptics::kVoiceCapacity;
                    for (size_t i = 0; i < halo3_haptics::kVoiceCapacity; ++i)
                        if (halo3_haptics::Expired(feature.voices[i])) { freeSlot = i; break; }
                    if (freeSlot == halo3_haptics::kVoiceCapacity)
                    {
                        std::array<float, halo3_haptics::kVoiceCapacity> ages{};
                        for (size_t i = 0; i < ages.size(); ++i)
                            ages[i] = feature.voices[i].ageSeconds;
                        // The native eight-entry queue also replaces its
                        // greatest-age record. Mirror that bounded policy so
                        // a full private bank does not leak recoil natively.
                        freeSlot = halo3_haptics::SelectOldest(ages);
                    }
                    // Native worker already evicted the same greatest-age
                    // entry it would replace in stock. Its evaluator skips
                    // UINT32_MAX before resolving row/curve data; clearing
                    // just the newly written datum preserves that exact
                    // native eviction while routing only the captured pulse.
                    *reinterpret_cast<uint32_t*>(record) = UINT32_MAX;
                    feature.voices[freeSlot] = pending;
                    ++feature.captured;
                    ++feature.retired;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    postFault = true;
                }
            }
                __finally { feature.writer.clear(std::memory_order_release); }
            }
            if (postFault)
            {
                feature.faulted.store(true, std::memory_order_release);
                ++feature.faults;
            }
    }
}

__declspec(noinline) void __fastcall Halo3WeaponHapticWorkerDetour(uint32_t owner,
    int32_t user, uint32_t datum, uint64_t fourth, uint64_t fifth,
    uint32_t sixth, float scale, uint8_t eighth)
{
    auto& feature = g_halo3WeaponHaptics;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        Halo3WeaponHapticWorkerBody(owner,user,datum,fourth,fifth,sixth,scale,
            eighth,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
    __finally { feature.callbacks.fetch_sub(1, std::memory_order_release); }
}

__declspec(noinline) uint64_t __fastcall Halo3WeaponHapticEvaluatorDetour(uintptr_t queue)
{
    auto& feature = g_halo3WeaponHaptics;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    volatile bool identityFault = false;
    __try
    {
        if (feature.evaluatorOriginal)
            result = reinterpret_cast<Halo3HapticEvaluatorFn>(feature.evaluatorOriginal)(queue);
        __try
        {
            uint8_t* localQueue = nullptr;
            if (Halo3WeaponHapticQueue(0, localQueue) &&
                localQueue == reinterpret_cast<uint8_t*>(queue))
                g_halo3WeaponHapticEvalObserved = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            identityFault = true;
        }
        if (identityFault)
        {
            feature.faulted.store(true, std::memory_order_release);
            ++feature.faults;
        }
    }
    __finally { feature.callbacks.fetch_sub(1, std::memory_order_release); }
    return result;
}

void Halo3WeaponHapticsRetireVoices() noexcept
{
    auto& feature = g_halo3WeaponHaptics;
    if (feature.writer.test_and_set(std::memory_order_acquire)) return;
    for (auto& voice : feature.voices) voice = {};
    feature.writer.clear(std::memory_order_release);
}

void Halo3WeaponHapticsTick(float dt) noexcept
{
    auto& feature = g_halo3WeaponHaptics;
    if (!feature.enabled.load(std::memory_order_acquire) ||
        feature.faulted.load(std::memory_order_acquire) ||
        feature.writer.test_and_set(std::memory_order_acquire)) return;
    if (!std::isfinite(dt) || dt <= 0 || dt > 0.25f)
    {
        for (auto& voice : feature.voices) voice = {};
        feature.writer.clear(std::memory_order_release);
        return;
    }
    volatile bool tickFault = false;
    __try
    {
        uint32_t owner = UINT32_MAX, weapons[2]{};
        const uint64_t nowMs = GetTickCount64();
        const bool ready = g_halo3PlayerUnitGetter &&
            g_vrAim.load(std::memory_order_acquire) &&
            feature.generation == g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        if (ready)
        {
            owner = static_cast<uint32_t>(g_halo3PlayerUnitGetter(0));
            if (owner == UINT32_MAX || !Halo3ReadOwnedWeapons(owner, weapons, false)) owner = UINT32_MAX;
        }
        float lowSum[2]{}, highSum[2]{};
        const uint64_t currentTokens[2]{
            VR_WeaponHapticToken(GameTitle::Halo3, feature.generation, false),
            VR_WeaponHapticToken(GameTitle::Halo3, feature.generation, true)};
        for (auto& voice : feature.voices)
        {
            if (!voice.active) continue;
            if (owner == UINT32_MAX || voice.owner != owner ||
                voice.generation != feature.generation ||
                !halo3_haptics::FreshAt(voice, nowMs) ||
                voice.slot >= 2 || !voice.sourceToken ||
                voice.sourceToken != currentTokens[voice.slot] ||
                ResolveEquippedWeaponSlot(voice.weapon, weapons[0], weapons[1],
                    true, weapons[1] != UINT32_MAX) != static_cast<int>(voice.slot))
            { voice = {}; continue; }
            float low = 0, high = 0;
            if (!halo3_haptics::Sample(voice,
                    reinterpret_cast<halo3_haptics::CurveFunction>(
                        feature.base + halo3_haptic_contract::haptic_curve), low, high))
            { voice = {}; continue; }
            const unsigned slot = voice.slot;
            lowSum[slot] += low;
            highSum[slot] += high;
            voice.ageSeconds += dt;
            voice.lastUpdateMs = nowMs;
            if (halo3_haptics::Expired(voice)) voice = {};
        }
        const float amplitude[2]{
            halo3_haptics::MixBands(lowSum[0], highSum[0]),
            halo3_haptics::MixBands(lowSum[1], highSum[1])};
        bool support = false;
        if (amplitude[0] > 0 && weapons[1] == UINT32_MAX)
        {
            SupportGripRelationshipSnapshot relation{};
            const bool currentRelationship = VR_GetSupportGripRelationship(relation) && relation.engaged &&
                relation.title == GameTitle::Halo3 && relation.generation == feature.generation &&
                relation.unit == owner && relation.weapon == weapons[0];
            support = currentRelationship ||
                (!g_config.persistent_support_grip && VR_IsTwoHandAiming());
        }
        if (amplitude[0] > 0)
            (void)VR_PulseWeaponHaptics(GameTitle::Halo3, feature.generation, false,
                support, amplitude[0], currentTokens[0]);
        if (amplitude[1] > 0)
            (void)VR_PulseWeaponHaptics(GameTitle::Halo3, feature.generation, true,
                false, amplitude[1], currentTokens[1]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        tickFault = true;
    }
    if (tickFault)
    {
        feature.faulted.store(true, std::memory_order_release);
        ++feature.faults;
    }
    feature.writer.clear(std::memory_order_release);
}

__declspec(noinline) void __fastcall Halo3WeaponHapticUpdateDetour(float dt)
{
    auto& feature = g_halo3WeaponHaptics;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const bool previousObserved = g_halo3WeaponHapticEvalObserved;
    g_halo3WeaponHapticEvalObserved = false;
    volatile bool tickFault = false;
    __try
    {
        if (feature.updateOriginal)
            reinterpret_cast<Halo3HapticUpdateFn>(feature.updateOriginal)(dt);
        const bool localEvaluatorRan = g_halo3WeaponHapticEvalObserved;
        g_halo3WeaponHapticEvalObserved = previousObserved;
        if (localEvaluatorRan)
        {
            __try { Halo3WeaponHapticsTick(dt); }
            __except (EXCEPTION_EXECUTE_HANDLER) { tickFault = true; }
        }
        else Halo3WeaponHapticsRetireVoices();
        if (tickFault)
        { feature.faulted.store(true, std::memory_order_release); ++feature.faults; }
    }
    __finally
    {
        g_halo3WeaponHapticEvalObserved = previousObserved;
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}

bool RemoveHalo3WeaponHaptics()
{
    auto& feature = g_halo3WeaponHaptics;
    feature.enabled.store(false, std::memory_order_release);
    void** targets[]{&feature.weaponTarget, &feature.triggerUpdateTarget, &feature.wrapperTarget, &feature.playerEffectTarget,
        &feature.workerTarget, &feature.evaluatorTarget, &feature.updateTarget};
    for (auto target : targets)
    {
        if (!*target) continue;
        const auto status = MCCVR_DisableHookForRetirement(*target);
        if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
        { LOG("Halo 3 weapon haptics CleanupRequired: hook disable failed"); return false; }
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo3WeaponHapticShotDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticTriggerUpdateDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticDamageWrapperDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticEffectDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticWorkerDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticEvaluatorDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticUpdateDetour)};
    const void* originals[]{feature.weaponOriginal, feature.triggerUpdateOriginal, feature.wrapperOriginal,
        feature.playerEffectOriginal,
        feature.workerOriginal, feature.evaluatorOriginal, feature.updateOriginal};
    if (!WaitForNativeDetourQuiescence(functions, originals, 7, feature.callbacks))
    { LOG("Halo 3 weapon haptics CleanupRequired: callbacks or ingress busy"); return false; }
    for (auto target : targets)
    {
        if (!*target) continue;
        const auto status = MH_RemoveHook(*target);
        if (status != MH_OK && status != MH_ERROR_NOT_CREATED)
        { LOG("Halo 3 weapon haptics CleanupRequired: hook removal failed"); return false; }
        *target = nullptr;
    }
    feature.weaponOriginal = feature.triggerUpdateOriginal = feature.wrapperOriginal = feature.playerEffectOriginal = feature.workerOriginal =
        feature.evaluatorOriginal = feature.updateOriginal = nullptr;
    feature.writer.test_and_set(std::memory_order_acquire);
    for (auto& voice : feature.voices) voice = {};
    feature.writer.clear(std::memory_order_release);
    feature.generation = 0;
    return true;
}

bool InstallHalo3WeaponHaptics(uintptr_t base, size_t size, uint32_t generation)
{
    auto& feature = g_halo3WeaponHaptics;
    if (feature.weaponTarget || feature.triggerUpdateTarget || feature.wrapperTarget || feature.playerEffectTarget || feature.workerTarget ||
        feature.evaluatorTarget || feature.updateTarget ||
        !generation || size <= halo3_haptic_contract::haptic_curve + 0x1000) return false;
    for (const auto& entry : halo3_haptic_contract::entries)
    {
        const uintptr_t hit = sig::Find(base, size, entry.pattern);
        if (hit != base + entry.rva || sig::Find(hit + 1, base + size - hit - 1, entry.pattern))
        { LOG("Halo 3 weapon haptics StockFallback: missing/ambiguous %s", entry.name); return false; }
    }
    for (const auto& edge : halo3_haptic_contract::relatives)
    {
        const auto* call = reinterpret_cast<const uint8_t*>(base + edge.rva);
        if (call[0] != 0xE8 || base + edge.rva + edge.size +
                *reinterpret_cast<const int32_t*>(call + edge.displacement) != base + edge.target)
        { LOG("Halo 3 weapon haptics StockFallback: caller edge mismatch at +0x%X", edge.rva); return false; }
    }
    for (const auto& witness : halo3_haptic_contract::witnesses)
    {
        const uintptr_t hit = sig::Find(base, size, witness.pattern);
        if (hit != base + witness.rva ||
            sig::Find(hit + 1, base + size - hit - 1, witness.pattern))
        { LOG("Halo 3 weapon haptics StockFallback: queue-write witness mismatch at +0x%X",witness.rva); return false; }
    }
    if (!g_engineTlsIndex || !g_halo3PlayerUnitGetter || !g_halo3TagDataBase ||
        !g_halo3TagInstanceTable)
    { LOG("Halo 3 weapon haptics StockFallback: local-player/tag/TLS bindings unavailable"); return false; }

    feature.base = base;
    feature.generation = generation;
    feature.faulted.store(false, std::memory_order_release);
    feature.captured.store(0); feature.retired.store(0); feature.fallback.store(0); feature.faults.store(0);
    for (auto& voice : feature.voices) voice = {};
    struct Binding { uint32_t rva; void* detour; void** original; void** target; };
    const Binding hooks[]{
        {halo3_haptic_contract::successful_weapon_fire,reinterpret_cast<void*>(&Halo3WeaponHapticShotDetour),&feature.weaponOriginal,&feature.weaponTarget},
        {halo3_haptic_contract::weapon_trigger_update,reinterpret_cast<void*>(&Halo3WeaponHapticTriggerUpdateDetour),&feature.triggerUpdateOriginal,&feature.triggerUpdateTarget},
        {halo3_haptic_contract::damage_effect_wrapper,reinterpret_cast<void*>(&Halo3WeaponHapticDamageWrapperDetour),&feature.wrapperOriginal,&feature.wrapperTarget},
        {halo3_haptic_contract::player_effect,reinterpret_cast<void*>(&Halo3WeaponHapticEffectDetour),&feature.playerEffectOriginal,&feature.playerEffectTarget},
        {halo3_haptic_contract::effect_worker,reinterpret_cast<void*>(&Halo3WeaponHapticWorkerDetour),&feature.workerOriginal,&feature.workerTarget},
        {halo3_haptic_contract::haptic_evaluator,reinterpret_cast<void*>(&Halo3WeaponHapticEvaluatorDetour),&feature.evaluatorOriginal,&feature.evaluatorTarget},
        {halo3_haptic_contract::haptic_update,reinterpret_cast<void*>(&Halo3WeaponHapticUpdateDetour),&feature.updateOriginal,&feature.updateTarget}};
    for (const auto& hook : hooks)
    {
        void* const target = reinterpret_cast<void*>(base + hook.rva);
        if (MH_CreateHook(target, hook.detour, hook.original) != MH_OK)
        { LOG("Halo 3 weapon haptics StockFallback: hook create failed at +0x%X",hook.rva); (void)RemoveHalo3WeaponHaptics(); return false; }
        *hook.target = target;
    }
    for (auto target : {feature.weaponTarget,feature.triggerUpdateTarget,feature.wrapperTarget,feature.playerEffectTarget,feature.workerTarget,
            feature.evaluatorTarget,feature.updateTarget})
    {
        if (MH_EnableHook(target) != MH_OK)
        { LOG("Halo 3 weapon haptics StockFallback: hook enable failed"); (void)RemoveHalo3WeaponHaptics(); return false; }
    }
    feature.enabled.store(true, std::memory_order_release);
    LOG("Halo 3 authored weapon haptics installed: admitted shots plus exact local charge effects, per-weapon role, stock evaluator admission");
    return true;
}

void ReportHalo3WeaponHaptics()
{
    const auto& feature = g_halo3WeaponHaptics;
    if (!feature.weaponTarget && !feature.triggerUpdateTarget && !feature.wrapperTarget && !feature.playerEffectTarget && !feature.workerTarget &&
        !feature.evaluatorTarget && !feature.updateTarget) return;
    LOG("Halo 3 authored weapon haptics: enabled=%d isolatedFault=%d captured=%llu retired=%llu nativeFallback=%llu faults=%llu",
        feature.enabled.load()?1:0,feature.faulted.load()?1:0,feature.captured.load(),
        feature.retired.load(),feature.fallback.load(),feature.faults.load());
    bool retained = feature.weaponTarget || feature.triggerUpdateTarget || feature.wrapperTarget ||
        feature.playerEffectTarget || feature.workerTarget || feature.evaluatorTarget || feature.updateTarget;
    if (retained && (feature.faulted.load(std::memory_order_acquire) ||
        !feature.enabled.load(std::memory_order_acquire)))
    {
        const bool removed = RemoveHalo3WeaponHaptics();
        (void)removed;
        LOG("Halo 3 weapon haptics %s: optional hooks %s; camera unchanged",
            removed ? "StockFallback" : "CleanupRequired",
            removed ? "retired" : "retained for retry");
    }
}
