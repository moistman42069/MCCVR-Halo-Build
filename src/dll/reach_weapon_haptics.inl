// Reach admitted barrel-fire recoil. HREK -> pinned retail evidence and
// signature contract: docs/REACH-WEAPON-HAPTICS-2026-09-30.md.
#include "../common/reach_weapon_haptics_logic.h"
#include "../common/reach_haptic_contract.h"

namespace reach_weapon_haptics
{
using SourceFn = void(__fastcall*)(uint32_t, int32_t, const int32_t*);
using EffectFn = void(__fastcall*)(uint32_t, int32_t, float);
using QueueFn = void(__fastcall*)(int32_t, uint32_t, float, const void*);
using UpdateFn = void(__fastcall*)(float);
using EvaluateFn = uint64_t(__fastcall*)(const void*);
using AttenuationFn = void(__fastcall*)(void*);
using CurveInputFn = float(__fastcall*)(const void*, float, float);
using CurveOutputFn = float(__fastcall*)(const void*, float);
using PlayerUnitFn = ReachPlayerUnitByOutputUserFn;

struct Runtime
{
    uintptr_t base{};
    size_t size{};
    uint32_t generation{};
    void* targets[5]{};
    SourceFn sourceOriginal{};
    EffectFn effectOriginal{};
    QueueFn queueOriginal{};
    UpdateFn updateOriginal{};
    EvaluateFn evaluateOriginal{};
    AttenuationFn attenuation{};
    CurveInputFn curveInput{};
    CurveOutputFn curveOutput{};
    PlayerUnitFn playerUnit{};
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> captured{0}, fallback{0}, faults{0};
    std::atomic_flag writer = ATOMIC_FLAG_INIT;
    struct PrivateVoice
    {
        reach_haptics::Voice contract{};
        alignas(4) uint8_t native[0x34]{};
        uint64_t lastEvaluationMs{};
    } voices[reach_haptics::kVoiceCapacity]{};
} runtime;

thread_local reach_haptics::Source firingSource{};
thread_local reach_haptics::Source effectSource{};
thread_local bool observingUpdate = false;
thread_local bool localEvaluationObserved = false;

bool Current() noexcept
{
    return runtime.enabled.load(std::memory_order_acquire) &&
        !runtime.faulted.load(std::memory_order_acquire) &&
        g_reachCamera.armed.load(std::memory_order_acquire) &&
        !g_reachCamera.teardownRequested.load(std::memory_order_acquire) &&
        runtime.generation == g_reachCamera.generation.load(std::memory_order_acquire) &&
        g_enabled.load(std::memory_order_acquire) && g_vrAim.load(std::memory_order_acquire) &&
        VR_IsStereoEnabled() && !VR_IsCutsceneTheaterActive() && !exclusive_input::Active() &&
        TitleAdapter_GetActiveTitle() == GameTitle::HaloReach &&
        TitleAdapter_GetRuntimeMode() == RuntimeMode::Gameplay;
}

bool ReadSource(uint32_t weapon, reach_haptics::Source& out) noexcept
{
    out = {};
    if (!Current() || weapon == UINT32_MAX || !runtime.playerUnit)
        return false;
    const uint32_t owner = uint32_t(runtime.playerUnit(0));
    if (owner == UINT32_MAX || !(owner >> 16))
        return false;
    uint32_t weapons[2]{};
    if (!ReachReadMuzzleWeapons(owner, weapons)) return false;
    const int slot = ResolveEquippedWeaponSlot(weapon, weapons[0], weapons[1],
        true, weapons[1] != UINT32_MAX);
    if (slot < 0 || slot > 1) return false;
    out = {owner, weapon, runtime.generation, uint8_t(slot), true};
    return runtime.generation == g_reachCamera.generation.load(std::memory_order_acquire);
}

bool LoadBands(uint32_t datum, uint8_t (&out)[reach_haptics::kTagCurveBytes]) noexcept
{
    if (datum == UINT32_MAX || runtime.size <= 0x4E39F20 + 16 ||
        runtime.size <= 0xC1A600 + 8)
        return false;
    volatile bool copied = false;
    __try
    {
        // The effect worker's authored field is an rmbl tag datum; use the
        // engine's exact packed tag token and leave both mappings opaque.
        const auto* table = *reinterpret_cast<const uint8_t* const*>(runtime.base + 0xC1A600);
        if (!table) return false;
        const uint32_t token = *reinterpret_cast<const uint32_t*>(
            table + 4 + size_t(datum & 0xFFFFu) * 8);
        const auto* segments = reinterpret_cast<const uint8_t* const*>(runtime.base + 0x4E39F20);
        const uint32_t segment = token >> 28;
        const uint8_t* segmentBase = segments[segment];
        if (segmentBase && token != UINT32_MAX)
        {
            std::memcpy(out, segmentBase + uint64_t(token) * 4, sizeof(out));
            copied = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { copied = false; }
    return copied != false;
}

bool ReadQueueBase(const void*& out) noexcept
{
    out = nullptr;
    if (!runtime.base || runtime.size <= 0xC17B18) return false;
    volatile bool copied = false;
    __try
    {
        auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
        const uint32_t index = *reinterpret_cast<const uint32_t*>(runtime.base + 0xC17B18);
        const auto* tls = slots && index < 0x200 ? static_cast<const uint8_t*>(slots[index]) : nullptr;
        if (tls)
        {
            out = *reinterpret_cast<const void* const*>(tls + 0x2D8);
            copied = out != nullptr;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out = nullptr; copied = false; }
    return copied != false;
}

bool SameSource(const reach_haptics::Source& a, const reach_haptics::Source& b) noexcept
{
    return reach_haptics::SameSource(a, b);
}

__declspec(noinline) reach_haptics::Source CaptureSource(uint32_t weapon, int32_t barrel) noexcept
{
    reach_haptics::Source out{};
    volatile bool faulted = false;
    __try { if (barrel >= 0 && barrel < 16) (void)ReadSource(weapon, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; }
    if (faulted) { runtime.faulted.store(true, std::memory_order_release); ++runtime.faults; out = {}; }
    return out;
}

__declspec(noinline) void __fastcall SourceHook(uint32_t weapon, int32_t barrel,
    const int32_t* selected)
{
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const auto previous = firingSource;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    firingSource = caller == runtime.base + reach_haptic_contract::admitted_effect_call + 5
        ? CaptureSource(weapon, barrel) : reach_haptics::Source{};
    __try { if (runtime.sourceOriginal) runtime.sourceOriginal(weapon, barrel, selected); }
    __finally { firingSource = previous; runtime.callbacks.fetch_sub(1, std::memory_order_release); }
}

__declspec(noinline) void __fastcall EffectHook(uint32_t owner, int32_t tag, float scale)
{
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const auto previous = effectSource;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    reach_haptics::Source candidate{};
    if ((caller == runtime.base + reach_haptic_contract::primary_effect_call + 5 ||
        caller == runtime.base + reach_haptic_contract::secondary_effect_call + 5) &&
        firingSource.valid && firingSource.owner == owner && Current())
        candidate = firingSource;
    effectSource = candidate;
    __try { if (runtime.effectOriginal) runtime.effectOriginal(owner, tag, scale); }
    __finally { effectSource = previous; runtime.callbacks.fetch_sub(1, std::memory_order_release); }
}

int StageQueueVoice(int32_t user, uint32_t tag, float scale, const void* descriptor,
    uintptr_t caller, const uint8_t* before, const uint8_t* after) noexcept
{
    if (user != 0 || caller != runtime.base + reach_haptic_contract::local_queue_call + 5 ||
        !effectSource.valid || !std::isfinite(scale) || scale < 0 || !descriptor ||
        !before || !after || !Current()) return -1;
    const int nativeSlot = reach_haptics::FindNewNativeVoice(before, after,
        tag, scale, static_cast<const uint8_t*>(descriptor));
    if (nativeSlot < 0) return -1;
    reach_haptics::Source current{};
    if (!ReadSource(effectSource.weapon, current) || !SameSource(effectSource, current)) return -1;
    reach_haptics::Voice voice{};
    uint8_t bands[reach_haptics::kTagCurveBytes]{};
    if (!LoadBands(tag, bands)) return -1;
    const uint64_t token = VR_WeaponHapticToken(GameTitle::HaloReach,
        current.generation, current.slot == 1);
    if (!reach_haptics::Capture(0, effectSource, current, tag, scale,
        static_cast<const uint8_t*>(descriptor), token, voice)) return -1;
    std::memcpy(voice.tagCurve.data(), bands, sizeof(bands));
    if (!reach_haptics::Valid(voice)) return -1;
    Runtime::PrivateVoice pending{};
    pending.contract = voice;
    pending.lastEvaluationMs = GetTickCount64();
    std::memcpy(pending.native, &tag, sizeof(tag));
    std::memcpy(pending.native + 4, &scale, sizeof(scale));
    std::memcpy(pending.native + 8, descriptor, reach_haptics::kSourceDescriptorBytes);
    if (runtime.writer.test_and_set(std::memory_order_acquire)) return -1;
    int stagedSlot = -1;
    __try
    {
        unsigned chosen = 0;
        for (unsigned i = 0; i < reach_haptics::kVoiceCapacity; ++i)
        {
            if (!runtime.voices[i].contract.active) { chosen = i; break; }
            if (runtime.voices[i].contract.ageSeconds > runtime.voices[chosen].contract.ageSeconds)
                chosen = i;
        }
        runtime.voices[chosen] = pending;
        stagedSlot = static_cast<int>(chosen);
    }
    __finally { runtime.writer.clear(std::memory_order_release); }
    ++runtime.captured;
    return stagedSlot;
}

void RollbackStaged(int slot, uint64_t token) noexcept
{
    if (slot < 0 || slot >= static_cast<int>(reach_haptics::kVoiceCapacity) ||
        runtime.writer.test_and_set(std::memory_order_acquire)) return;
    if (runtime.voices[slot].contract.sourceToken == token) runtime.voices[slot] = {};
    runtime.writer.clear(std::memory_order_release);
}

__declspec(noinline) void __fastcall QueueHook(int32_t user, uint32_t tag, float scale,
    const void* descriptor)
{
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    alignas(16) uint8_t before[reach_haptics::kNativeUserRowBytes]{};
    const void* manager = nullptr;
    bool beforeValid = false;
    volatile bool readFault = false;
    __try
    {
        if (caller == runtime.base + reach_haptic_contract::local_queue_call + 5 &&
            user == 0 && effectSource.valid && ReadQueueBase(manager))
        { std::memcpy(before, manager, sizeof(before)); beforeValid = true; }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { readFault = true; }
    if (readFault) { runtime.faulted.store(true, std::memory_order_release); ++runtime.faults; }
    __try
    {
        // Preserve native slot selection and eviction by always calling the
        // original exactly once. Only a uniquely identified new record can be
        // diverted after the native writer returns.
        if (runtime.queueOriginal) runtime.queueOriginal(user, tag, scale, descriptor);
        if (beforeValid)
        {
            volatile bool routed = false, postFault = false;
            int staged = -1;
            uint64_t stagedToken = 0;
            __try
            {
                alignas(16) uint8_t after[reach_haptics::kNativeUserRowBytes]{};
                std::memcpy(after, manager, sizeof(after));
                staged = StageQueueVoice(user, tag, scale, descriptor, caller, before, after);
                if (staged >= 0)
                {
                    stagedToken = runtime.voices[staged].contract.sourceToken;
                    uint8_t* record = static_cast<uint8_t*>(const_cast<void*>(manager)) + size_t(staged) * 0x34;
                    const bool stillMatches = std::memcmp(record, &tag, sizeof(tag)) == 0 &&
                        std::memcmp(record + 4, &scale, sizeof(scale)) == 0 &&
                        std::memcmp(record + 8, descriptor, reach_haptics::kSourceDescriptorBytes) == 0;
                    if (stillMatches)
                    {
                        auto* nativeTag = reinterpret_cast<volatile long*>(record);
                        routed = InterlockedCompareExchange(nativeTag, long(UINT32_MAX),
                            long(tag)) == long(tag);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { postFault = true; }
            if (postFault)
            {
                runtime.faulted.store(true, std::memory_order_release); ++runtime.faults;
                if (staged >= 0) RollbackStaged(staged, stagedToken);
            }
            else if (staged >= 0 && !routed) RollbackStaged(staged, stagedToken);
            if (!routed && effectSource.valid) ++runtime.fallback;
        }
    }
    __finally { runtime.callbacks.fetch_sub(1, std::memory_order_release); }
}

__declspec(noinline) uint64_t __fastcall EvaluateHook(const void* queue)
{
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        if (runtime.evaluateOriginal) result = runtime.evaluateOriginal(queue);
        if (observingUpdate && Current())
        {
            const void* own = nullptr;
            if (ReadQueueBase(own) && own == queue) localEvaluationObserved = true;
        }
    }
    __finally { runtime.callbacks.fetch_sub(1, std::memory_order_release); }
    return result;
}

void UpdatePrivateBody(float dt)
{
    if (!runtime.writer.test_and_set(std::memory_order_acquire))
    {
        __try
        {
            if (!Current() || !std::isfinite(dt) || dt <= 0 || dt > 0.25f)
            {
                for (auto& voice : runtime.voices) voice = {};
                return;
            }
            float motors[2][2]{};
            reach_haptics::Source owners[2]{};
            uint64_t tokens[2]{};
            const uint64_t now = GetTickCount64();
            for (auto& entry : runtime.voices)
            {
                auto& voice = entry.contract;
                if (!voice.active) continue;
                reach_haptics::Source current{};
                if (now < entry.lastEvaluationMs || now - entry.lastEvaluationMs > 100 ||
                    !ReadSource(voice.weapon, current) || !SameSource(current,
                        reach_haptics::Source{voice.owner, voice.weapon, voice.generation,
                            voice.slot, true}) ||
                    VR_WeaponHapticToken(GameTitle::HaloReach, current.generation,
                        current.slot == 1) != voice.sourceToken)
                { entry = {}; continue; }
                runtime.attenuation(entry.native + 8);
                entry.lastEvaluationMs = now;
                std::memcpy(voice.descriptor.data(), entry.native + 8,
                    reach_haptics::kSourceDescriptorBytes);
                if (!reach_haptics::Advance(voice, dt)) { entry = {}; continue; }
                float low = 0, high = 0;
                const auto evaluate = [](const void* map, float age, float endpoint) -> float {
                    const float input = reach_weapon_haptics::runtime.curveInput(map, age, endpoint);
                    return reach_weapon_haptics::runtime.curveOutput(map, input);
                };
                if (!reach_haptics::Sample(voice, evaluate, low, high) ||
                    (!low && !high)) { if (reach_haptics::Expired(voice)) entry = {}; continue; }
                motors[current.slot][0] += low; motors[current.slot][1] += high;
                owners[current.slot] = current; tokens[current.slot] = voice.sourceToken;
            }
            for (int slot = 0; slot < 2; ++slot)
            {
                if (!owners[slot].valid) continue;
                const auto q = [](float v) { return uint16_t(std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f); };
                const float amplitude = q(motors[slot][0]) * (0.65f / 65535.0f) +
                    q(motors[slot][1]) * (0.35f / 65535.0f);
                if (amplitude > 0)
                    (void)VR_PulseWeaponHaptics(GameTitle::HaloReach,
                        owners[slot].generation, slot == 1,
                        slot == 0 && Supported(owners[slot]), amplitude, tokens[slot]);
            }
        }
        __finally { runtime.writer.clear(std::memory_order_release); }
    }
}

void ClearPrivateVoices() noexcept
{
    if (runtime.writer.test_and_set(std::memory_order_acquire)) return;
    for (auto& voice : runtime.voices) voice = {};
    runtime.writer.clear(std::memory_order_release);
}

__declspec(noinline) void UpdatePrivate(float dt)
{
    volatile bool faulted = false;
    __try { UpdatePrivateBody(dt); }
    __except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; }
    if (faulted)
    {
        runtime.faulted.store(true, std::memory_order_release);
        ++runtime.faults;
        if (!runtime.writer.test_and_set(std::memory_order_acquire))
        {
            for (auto& voice : runtime.voices) voice = {};
            runtime.writer.clear(std::memory_order_release);
        }
    }
}

__declspec(noinline) void __fastcall UpdateHook(float dt)
{
    runtime.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const bool oldObserving = observingUpdate, oldEvaluated = localEvaluationObserved;
    observingUpdate = true; localEvaluationObserved = false;
    __try
    {
        if (runtime.updateOriginal) runtime.updateOriginal(dt);
        if (localEvaluationObserved) UpdatePrivate(dt);
        else ClearPrivateVoices();
    }
    __finally
    {
        observingUpdate = oldObserving; localEvaluationObserved = oldEvaluated;
        runtime.callbacks.fetch_sub(1, std::memory_order_release);
    }
}
} // namespace reach_weapon_haptics

bool RemoveReachWeaponHaptics()
{
    using namespace reach_weapon_haptics;
    auto& r = runtime; r.enabled.store(false, std::memory_order_release);
    for (void* target : r.targets)
    {
        if (!target) continue;
        const MH_STATUS status = MCCVR_DisableHookForRetirement(target);
        if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
        { LOG("Reach weapon haptics CleanupRequired: disable failed"); return false; }
    }
    const void* functions[]{reinterpret_cast<const void*>(SourceHook),
        reinterpret_cast<const void*>(EffectHook), reinterpret_cast<const void*>(QueueHook),
        reinterpret_cast<const void*>(UpdateHook), reinterpret_cast<const void*>(EvaluateHook)};
    const void* originals[]{reinterpret_cast<const void*>(r.sourceOriginal),
        reinterpret_cast<const void*>(r.effectOriginal), reinterpret_cast<const void*>(r.queueOriginal),
        reinterpret_cast<const void*>(r.updateOriginal), reinterpret_cast<const void*>(r.evaluateOriginal)};
    if (!WaitForNativeDetourQuiescence(functions, originals, 5, r.callbacks))
    { LOG("Reach weapon haptics CleanupRequired: callbacks or native ingress busy"); return false; }
    for (void*& target : r.targets)
    {
        if (!target) continue;
        const MH_STATUS status = MH_RemoveHook(target);
        if (status != MH_OK && status != MH_ERROR_NOT_CREATED)
        { LOG("Reach weapon haptics CleanupRequired: hook removal failed"); return false; }
        target = nullptr;
    }
    r.sourceOriginal = nullptr; r.effectOriginal = nullptr; r.queueOriginal = nullptr;
    r.updateOriginal = nullptr; r.evaluateOriginal = nullptr;
    r.attenuation = nullptr; r.curveInput = nullptr; r.curveOutput = nullptr;
    r.playerUnit = nullptr;
    for (auto& voice : r.voices) voice = {};
    r.base = 0; r.size = 0; r.generation = 0;
    return true;
}

bool InstallReachWeaponHaptics(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace reach_weapon_haptics;
    auto& r = runtime;
    for (void* target : r.targets) if (target) return false;
    if (!base || !generation || size <= 0x4E39F30) return false;
    for (const auto& binding : reach_haptic_contract::entries)
    {
        const uintptr_t hit = sig::Find(base, size, binding.pattern);
        if (hit != base + binding.rva || sig::Find(hit + 1, base + size - hit - 1, binding.pattern))
        { LOG("Reach weapon haptics StockFallback: signature +%X missing/ambiguous", binding.rva); return false; }
    }
    for (const auto& edge : reach_haptic_contract::edges)
        if (*reinterpret_cast<const uint8_t*>(base + edge.call) != 0xE8 ||
            base + edge.call + 5 + *reinterpret_cast<const int32_t*>(base + edge.call + 1) != base + edge.target)
        { LOG("Reach weapon haptics StockFallback: call edge mismatch at +%X", edge.call); return false; }
    for (const auto& witness : reach_haptic_contract::witnesses)
        if (sig::Find(base + witness.rva, 64, witness.pattern) != base + witness.rva)
        { LOG("Reach weapon haptics StockFallback: witness mismatch at +%X", witness.rva); return false; }
    static constexpr char kPlayerUnitAob[] =
        "83 C8 FF 83 F9 03 77 27 8B 15 12 3C BC 00 65 48 8B 04 25 58 00 00 00 41 B8 58 01 00 00 48 63 C9 48 8B 04 D0 4A 8B 04 00 8B 84 88 C8 00 00 00 C3";
    static constexpr char kTagGetAob[] =
        "4C 8B 15 ?? ?? ?? ?? 45 33 DB 4C 0F BF C2 45 8B CB 49 63 82 F8 FF 03 00 4C 3B C0 73 17 48 8B 05 ?? ?? ?? ?? C1 FA 10 4E 8D 04 C0 66 41 3B 50 02 4D 0F 44 C8 4D 85 C9 74 3E 49 0F BF 01 48 03 C0 41 39 8C C2 FC FF 03 00 74 14 41 39 8C C2 00 00 04 00 74 0A 41 39 8C C2 04 00 04 00 75 19 41 8B 49 04 48 8D 15 ?? ?? ?? ?? 8B C1 48 C1 E8 1C 48 8B 04 C2 4C 8D 1C 88 49 8B C3 C3";
    const uintptr_t playerHit = sig::Find(base, size, kPlayerUnitAob);
    const uintptr_t tagHit = sig::Find(base, size, kTagGetAob);
    if (playerHit != base + kReachPlayerUnitByOutputUserRva ||
        sig::Find(playerHit + 1, base + size - playerHit - 1, kPlayerUnitAob) ||
        tagHit != base + kReachTagGetRva ||
        sig::Find(tagHit + 1, base + size - tagHit - 1, kTagGetAob))
    { LOG("Reach weapon haptics StockFallback: local-user/tag bindings missing or ambiguous"); return false; }
    r.base = base; r.size = size; r.generation = generation; r.faulted = false; r.writer.clear();
    r.attenuation = reinterpret_cast<AttenuationFn>(base + 0x163DA4);
    r.curveInput = reinterpret_cast<CurveInputFn>(base + 0x174ED8);
    r.curveOutput = reinterpret_cast<CurveOutputFn>(base + 0x175524);
    r.playerUnit = reinterpret_cast<PlayerUnitFn>(base + kReachPlayerUnitByOutputUserRva);
    const uintptr_t addresses[]{reach_haptic_contract::admitted_effect_source,
        reach_haptic_contract::authored_effect_wrapper, reach_haptic_contract::queue_writer,
        0x0DACD8, 0x0DB19C};
    void* detours[]{reinterpret_cast<void*>(SourceHook), reinterpret_cast<void*>(EffectHook),
        reinterpret_cast<void*>(QueueHook), reinterpret_cast<void*>(UpdateHook),
        reinterpret_cast<void*>(EvaluateHook)};
    void** originals[]{reinterpret_cast<void**>(&r.sourceOriginal), reinterpret_cast<void**>(&r.effectOriginal),
        reinterpret_cast<void**>(&r.queueOriginal), reinterpret_cast<void**>(&r.updateOriginal),
        reinterpret_cast<void**>(&r.evaluateOriginal)};
    for (unsigned i = 0; i < 5; ++i)
    {
        void* target = reinterpret_cast<void*>(base + addresses[i]);
        if (MH_CreateHook(target, detours[i], originals[i]) != MH_OK)
        { (void)RemoveReachWeaponHaptics(); LOG("Reach weapon haptics StockFallback: hook create failed"); return false; }
        r.targets[i] = target;
    }
    for (void* target : r.targets)
        if (MH_EnableHook(target) != MH_OK)
        { (void)RemoveReachWeaponHaptics(); LOG("Reach weapon haptics StockFallback: hook enable failed"); return false; }
    r.enabled.store(true, std::memory_order_release);
    LOG("Reach hand-specific weapon haptics installed: admitted barrel firing effects only; trigger/charge and general rumble remain native; headset verification pending");
    return true;
}

void ReportReachWeaponHaptics()
{
    const auto& r = reach_weapon_haptics::runtime;
    bool any = false; for (void* target : r.targets) any = any || target != nullptr;
    if (!any) return;
    LOG("Reach weapon haptics: enabled=%d fault=%d captured=%llu stockFallback=%llu faults=%llu",
        r.enabled.load() ? 1 : 0, r.faulted.load() ? 1 : 0,
        r.captured.load(), r.fallback.load(), r.faults.load());
    if (r.faulted.load() || !r.enabled.load()) (void)RemoveReachWeaponHaptics();
}
