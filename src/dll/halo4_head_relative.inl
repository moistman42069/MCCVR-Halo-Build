// HALO 4: THE MOTION TRACKER FOLLOWS THE HEAD -- the runtime half. The sites, the pure logic and the reasoning are
// in src/common/halo4_head_relative_logic.h; the write-up is docs/HALO4-HEAD-RELATIVE-2026-10-06.md.
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED. Every binding here is read from retail halo4.dll
// alone (no editing-kit symbols), and the names are ours. The lane therefore (a) binds nothing unless
// every static link holds on the loaded image, and (b) changes nothing until the argument it means to replace has
// agreed, at run time, with the aim yaw this mod's own direct drive is writing into the game (ArgumentProof;
// the reference is Halo4TrackerAimReference in game.cpp). Until then every call is forwarded untouched and
// counted as "unproven".
//
// In short: the tracker's player-centred build reads player control's yaw -- the record the direct drive writes
// the HAND into -- adds pi/2 and passes it as the eleventh argument of the builder. This lane hooks the builder
// and, for that one call, hands it the head's yaw instead. It writes no game memory at all.

// ---- THE HEAD, as the frame that rendered with it published it (the two-clocks rule) ---------------------------
// Written by the render thread (Halo4StereoTransaction, where the head-look camera is settled), read by the
// tracker's build. A sequence lock over atomics: an odd sequence is a write in progress, and a read that saw the
// sequence move is retried.
struct Halo4HeadPublication
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<float> forward[3]{}, up[3]{};
    // For the sound listener (halo4_sound_listener.inl): where that head-look camera is, the game's own camera
    // of the same frame before the head pose was applied -- the aim -- and where this frame's two eye cameras are.
    std::atomic<float> position[3]{}, aimForward[3]{}, aimPosition[3]{}, eyePosition[2][3]{};
    std::atomic<uint64_t> stampUs{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint32_t> flags{0};   // bit 0: the mod's head view was live; bit 1: the player had control
    std::atomic<uint32_t> thread{0};  // the publishing thread, for the dev report (not under the sequence)
} g_halo4HeadPublication;
constexpr uint32_t kHalo4HeadLive = 1u, kHalo4HeadGameplay = 2u;

// The render thread, once per stereo transaction. `forward` / `up` / `position`: the head-look camera in the
// game's world; `aimForward` / `aimPosition`: the game's own camera of this frame before the head pose (the aim);
// `leftEye` / `rightEye`: where the two eye cameras this transaction is about to render from are; `headTracked`:
// the head pose steered it in this frame (false in the cutscene theatre and when no pose was applied);
// `gameplay`: the game says the player has control (no cinematic).
void Halo4PublishHeadSample(const float* forward, const float* up, const float* position, const float* aimForward,
                            const float* aimPosition, const float* leftEye, const float* rightEye, bool headTracked,
                            bool gameplay, uint32_t generation) noexcept
{
    auto& published = g_halo4HeadPublication;
    const bool live = headTracked && generation && head_relative::Finite3(forward) && head_relative::Finite3(up);
    const uint32_t flags = (live ? kHalo4HeadLive : 0u) | (gameplay ? kHalo4HeadGameplay : 0u);
    const uint64_t nowUs = Sight_NowUs();
    const uint32_t sequence = published.sequence.load();
    published.sequence.store(sequence + 1);
    for (int i = 0; i < 3; ++i)
    {
        published.forward[i].store(forward ? forward[i] : 0.0f);
        published.up[i].store(up ? up[i] : 0.0f);
        // A position that was not handed over reads as not finite, so a consumer that needs it refuses.
        published.position[i].store(position ? position[i] : std::numeric_limits<float>::quiet_NaN());
        published.aimForward[i].store(aimForward ? aimForward[i] : 0.0f);
        published.aimPosition[i].store(aimPosition ? aimPosition[i] : std::numeric_limits<float>::quiet_NaN());
        published.eyePosition[0][i].store(leftEye ? leftEye[i] : std::numeric_limits<float>::quiet_NaN());
        published.eyePosition[1][i].store(rightEye ? rightEye[i] : std::numeric_limits<float>::quiet_NaN());
    }
    published.stampUs.store(nowUs);
    published.generation.store(generation);
    published.flags.store(flags);
    published.sequence.store(sequence + 2);
#if HALOMCCVR_DEV_DIAGNOSTICS
    published.thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
#endif
}
// Any thread. False = nothing coherent could be read (the writer was mid-write four times running).
bool Halo4ReadHeadSample(head_relative::HeadSample& sample, bool& gameplay) noexcept
{
    auto& published = g_halo4HeadPublication;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before = published.sequence.load();
        if (before & 1u)
            continue;
        for (int i = 0; i < 3; ++i)
        {
            sample.forward[i] = published.forward[i].load();
            sample.up[i] = published.up[i].load();
        }
        sample.stampUs = published.stampUs.load();
        sample.generation = published.generation.load();
        const uint32_t flags = published.flags.load();
        if (published.sequence.load() != before)
            continue;
        sample.valid = (flags & kHalo4HeadLive) != 0;
        gameplay = (flags & kHalo4HeadGameplay) != 0;
        return true;
    }
    return false;
}
// The same sample with the head's position and the frame's aim camera (the sound listener). Any thread.
bool Halo4ReadHeadFrame(halo4_sound_listener::HeadFrame& frame, bool& gameplay) noexcept
{
    auto& published = g_halo4HeadPublication;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before = published.sequence.load();
        if (before & 1u)
            continue;
        for (int i = 0; i < 3; ++i)
        {
            frame.sample.forward[i] = published.forward[i].load();
            frame.sample.up[i] = published.up[i].load();
            frame.position[i] = published.position[i].load();
            frame.aimForward[i] = published.aimForward[i].load();
            frame.aimPosition[i] = published.aimPosition[i].load();
            frame.eyePosition[0][i] = published.eyePosition[0][i].load();
            frame.eyePosition[1][i] = published.eyePosition[1][i].load();
        }
        frame.sample.stampUs = published.stampUs.load();
        frame.sample.generation = published.generation.load();
        const uint32_t flags = published.flags.load();
        if (published.sequence.load() != before)
            continue;
        frame.sample.valid = (flags & kHalo4HeadLive) != 0;
        gameplay = (flags & kHalo4HeadGameplay) != 0;
        return true;
    }
    return false;
}

// ---- THE TRACKER -----------------------------------------------------------------------------------------------
// Retail +0x4CFCB4: eleven arguments, an answer in al that both callers read. tools/audit_caller_area.py
// 4CFCB4:15:i is clean: it reads entry+0x30..+0x58 (arguments six to eleven; the fifth never), and neither caller
// keeps a volatile register across the call. On entry it reads rcx, edx, r8, r9 and no xmm register.
// The whole of rax goes back, so whatever the game puts in al reaches its caller unchanged.
using Halo4TrackerBuilderFn = uint64_t (__fastcall*)(void* state, int32_t player, void* third, void* fourth,
                                                     uint64_t fifth, void* sixth, float seventh, float eighth,
                                                     float ninth, const float* origin, float heading);
constexpr size_t kHalo4TrackerUseCount = static_cast<size_t>(halo4_head_relative::TrackerUse::Count);
constexpr size_t kHalo4ProofStepCount = static_cast<size_t>(halo4_head_relative::ProofStep::Count);
struct Halo4TrackerHeadRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    void* target = nullptr;
    void* original = nullptr;      // Halo4TrackerBuilderFn
    uintptr_t playerReturn = 0;    // where the player-centred build's call of the builder returns
    int32_t userFromState = 0;     // the component's user index, relative to the state argument
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> callbacks{0};
    // The window the report drains: every call lands in exactly one use.
    std::atomic<uint32_t> uses[kHalo4TrackerUseCount]{};
    std::atomic<uint32_t> steps[kHalo4ProofStepCount]{};
    std::atomic<uint32_t> faults{0}, mixedReads{0};
    std::atomic<float> lastNativeDeg{0.0f}, lastHeadDeg{0.0f}, lastReferenceDeg{0.0f};
    std::atomic<uint32_t> lastThread{0};
    std::atomic<uintptr_t> otherReturn{0};  // where the last call that was NOT the player-centred build's returns
    // The proof and the held heading belong to user 0's player-centred build. The game runs that build on its job
    // threads (three thread ids observed) and, as far as observed, one at a time.
    // `inside` makes "one at a time" a fact instead of an observation: a build of user 0 that arrives while
    // another is inside is forwarded untouched and counted (TrackerUse::Overlapped). `proven`, `provenCount` and
    // `withdrawnCount` are for the report.
    std::atomic<bool> inside{false};
    halo4_head_relative::ArgumentProof proof;  // under `inside`
    std::atomic<bool> proven{false};
    std::atomic<uint32_t> provenCount{0}, withdrawnCount{0};
    head_relative::HeadingHold hold;  // under `inside`
    // The report's thread only.
    int reportedSwitch = -1;
    uint32_t reportedProven = 0, reportedWithdrawn = 0;
    bool reportedFirstUse = false, reportedFirstRefusal = false, reportedOtherBuild = false;
} g_halo4TrackerHead;

// The tracker build's thread (one of the game's job threads), at the builder's entry. Decides the heading this
// call is handed: the game's own (`heading`) or the head's. No allocation, no logging, no waiting (one flag that is
// tried, never waited for), no write to game memory.
__declspec(noinline) float Halo4TrackerHeadingFor(const void* state, float heading, uintptr_t caller) noexcept
{
    using namespace halo4_head_relative;
    auto& feature = g_halo4TrackerHead;
    TrackerUse use = TrackerUse::NoSample;
    float handed = heading;
    volatile bool holding = false;  // this call is user 0's build and holds `inside` (the handler reads it too)
    __try
    {
        TrackerRequest request;
        request.playerBuild = caller == feature.playerReturn;
        if (request.playerBuild)
        {
            request.user = *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(state) + feature.userFromState);
            if (request.user == 0)
            {
                if (feature.inside.exchange(true, std::memory_order_acquire))
                    use = TrackerUse::Overlapped;
                else
                {
                    holding = true;
                    // The proof runs whatever the switch says: it is about the game, not about the player's choice.
                    float reference = 0.0f;
                    const bool usable = Halo4TrackerAimReference(reference);
                    const ProofStep step = StepProof(feature.proof, YawFromField(heading), usable, reference);
                    feature.steps[static_cast<size_t>(step)].fetch_add(1, std::memory_order_relaxed);
                    if (usable)
                        feature.lastReferenceDeg.store(WrapPi(reference) * head_relative::kRadToDeg,
                                                       std::memory_order_relaxed);
                    if (step == ProofStep::Proven)
                    {
                        feature.proven.store(true, std::memory_order_release);
                        feature.provenCount.fetch_add(1, std::memory_order_relaxed);
                    }
                    else if (step == ProofStep::Withdrawn)
                    {
                        feature.proven.store(false, std::memory_order_release);
                        feature.withdrawnCount.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }
        if (use != TrackerUse::Overlapped)
        {
            // Only user 0's build, holding `inside`, reads the proof or reaches the held heading: TrackerHeading
            // returns before `hold` for every other call.
            request.proven = holding && feature.proof.proven;
            request.switchOn = g_config.tracker_follows_head;
            request.sign = g_config.tracker_head_sign;
            request.offsetDeg = g_config.tracker_head_offset_deg;
            request.nowUs = Sight_NowUs();
            const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
            request.generation = generation == feature.generation ? generation : 0;
            head_relative::HeadSample sample;
            bool gameplay = false;
            if (!Halo4ReadHeadSample(sample, gameplay))
            {
                sample = head_relative::HeadSample{};
                feature.mixedReads.fetch_add(1, std::memory_order_relaxed);
            }
            request.gameplay = gameplay;
            float replaced = heading, headYaw = 0.0f;
            use = TrackerHeading(request, sample, feature.hold, heading, replaced, &headYaw);
            if (use == TrackerUse::Used)
            {
                handed = replaced;
                feature.lastHeadDeg.store(headYaw * head_relative::kRadToDeg, std::memory_order_relaxed);
            }
        }
        if (request.playerBuild)
            feature.lastNativeDeg.store(WrapPi(YawFromField(heading)) * head_relative::kRadToDeg,
                                        std::memory_order_relaxed);
        else
            feature.otherReturn.store(caller, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (holding)
            feature.inside.store(false, std::memory_order_release);
        feature.faults.fetch_add(1, std::memory_order_relaxed);
        return heading;
    }
    if (holding)
        feature.inside.store(false, std::memory_order_release);
    feature.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    feature.lastThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
#endif
    return handed;
}
__declspec(noinline) uint64_t __fastcall Halo4TrackerBuilderDetour(void* state, int32_t player, void* third,
                                                                  void* fourth, uint64_t fifth, void* sixth,
                                                                  float seventh, float eighth, float ninth,
                                                                  const float* origin, float heading)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto& feature = g_halo4TrackerHead;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t answer = 0;
    __try
    {
        if (state && feature.enabled.load(std::memory_order_acquire))
            heading = Halo4TrackerHeadingFor(state, heading, caller);
        if (const auto original = reinterpret_cast<Halo4TrackerBuilderFn>(feature.original))
            answer = CallNativeWithSpareSlots(original, state, player, third, fourth, fifth, sixth, seventh, eighth,
                                              ninth, origin, heading);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
    return answer;
}

// The game's bytes at a fixed place, as they stood before any hook of ours. False = unreadable.
__declspec(noinline) bool Halo4HeadRelativeCopy(uintptr_t address, void* out, size_t size) noexcept
{
    __try
    {
        (void)sig::CopyUnhooked(address, out, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
bool Halo4HeadRelativeRead(uintptr_t base, size_t size, uintptr_t address, void* out, size_t bytes)
{
    return address && address >= base && bytes <= size && address - base <= size - bytes &&
           Halo4HeadRelativeCopy(address, out, bytes);
}
// A pattern's one match in the image, or 0 when it is missing or matches twice.
uintptr_t Halo4HeadRelativeUnique(uintptr_t base, size_t size, const char* pattern)
{
    const uintptr_t match = sig::Find(base, size, pattern);
    if (!match || match + 1 >= base + size)
        return 0;
    return sig::Find(match + 1, base + size - match - 1, pattern) ? 0 : match;
}
// The beginning of the function `address` lies in, from the image's own function table, as an offset from
// `base`; -1 when the table has no entry for it or it belongs to another image.
int64_t Halo4HeadRelativeFunctionRva(uintptr_t base, uintptr_t address)
{
    DWORD64 imageBase = 0;
    const auto* function = RtlLookupFunctionEntry(address, &imageBase, nullptr);
    return function && imageBase == base ? static_cast<int64_t>(function->BeginAddress) : -1;
}

bool RemoveHalo4HeadRelative()
{
    auto& feature = g_halo4TrackerHead;
    feature.enabled.store(false, std::memory_order_release);
    if (!feature.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(feature.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("TRACKER HEAD Halo 4: CleanupRequired: disable failed (%d)", static_cast<int>(disabled));
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo4TrackerBuilderDetour)};
    const void* trampolines[]{feature.original};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, feature.callbacks))
    {
        LOG("TRACKER HEAD Halo 4: CleanupRequired: the detour is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(feature.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("TRACKER HEAD Halo 4: CleanupRequired: removal failed (%d)", static_cast<int>(removed));
        return false;
    }
    feature.target = nullptr;
    feature.original = nullptr;
    return true;
}
// Cold, on the title's install thread, after the camera core. Binds nothing unless every static link holds on
// this image; a refusal is the game's own tracker, said once.
bool InstallHalo4HeadRelative(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace halo4_head_relative;
    auto& feature = g_halo4TrackerHead;
    if (feature.target)
    {
        LOG("TRACKER HEAD Halo 4: StockFallback: the previous install was not retired");
        return false;
    }
    if (!base || !generation || size < 0x1000)
    {
        LOG("TRACKER HEAD Halo 4: StockFallback: no title image or generation");
        return false;
    }
    const uintptr_t method = Halo4HeadRelativeUnique(base, size, kTrackerMethodPattern);
    const uintptr_t site = Halo4HeadRelativeUnique(base, size, kTrackerHeadingSitePattern);
    const uintptr_t builder = Halo4HeadRelativeUnique(base, size, kTrackerBuilderHeadPattern);
    uint8_t methodBytes[kMethodBytes]{}, siteBytes[kSiteBytes]{}, builderBytes[kBuilderHeadBytes]{};
    uint8_t halfPiBytes[sizeof(float)]{};
    const bool methodRead = Halo4HeadRelativeRead(base, size, method, methodBytes, kMethodBytes) &&
                            Halo4HeadRelativeFunctionRva(base, method) == static_cast<int64_t>(method - base);
    const bool siteRead = Halo4HeadRelativeRead(base, size, site, siteBytes, kSiteBytes);
    const bool builderRead = Halo4HeadRelativeRead(base, size, builder, builderBytes, kBuilderHeadBytes) &&
                             Halo4HeadRelativeFunctionRva(base, builder) == static_cast<int64_t>(builder - base);
    bool halfPiRead = false;
    if (siteRead)
    {
        const int64_t halfPiRva = RipRva(siteBytes, kSiteHalfPiDisp, kSiteHalfPiNext, static_cast<int64_t>(site - base));
        halfPiRead = halfPiRva > 0 &&
                     Halo4HeadRelativeRead(base, size, base + static_cast<uintptr_t>(halfPiRva), halfPiBytes, sizeof(float));
    }
    TrackerSites sites;
    const TrackerBind bind =
        DecodeTrackerSites(methodRead ? methodBytes : nullptr, static_cast<int64_t>(method - base),
                           siteRead ? siteBytes : nullptr, static_cast<int64_t>(site - base),
                           siteRead ? Halo4HeadRelativeFunctionRva(base, site) : -1,
                           builderRead ? builderBytes : nullptr, static_cast<int64_t>(builder - base),
                           halfPiRead ? halfPiBytes : nullptr, sites);
    if (bind != TrackerBind::Bound)
    {
        LOG("TRACKER HEAD Halo 4: StockFallback: %s (method %s, heading site %s, builder %s); the game's own "
            "tracker. Retail-derived lead, not H4EK-verified",
            TrackerBindName(bind), method ? "matched" : "unmatched", site ? "matched" : "unmatched",
            builder ? "matched" : "unmatched");
        return false;
    }
    feature.base = base;
    feature.generation = generation;
    feature.playerReturn = base + static_cast<uintptr_t>(sites.returnRva);
    feature.userFromState = sites.userFromState;
    feature.proof = ArgumentProof{};
    feature.proven.store(false, std::memory_order_release);
    feature.provenCount.store(0, std::memory_order_relaxed);
    feature.withdrawnCount.store(0, std::memory_order_relaxed);
    feature.hold = head_relative::HeadingHold{};
    feature.reportedSwitch = -1;
    feature.reportedProven = feature.reportedWithdrawn = 0;
    feature.reportedFirstUse = feature.reportedFirstRefusal = feature.reportedOtherBuild = false;
    feature.otherReturn.store(0, std::memory_order_relaxed);
    for (auto& count : feature.uses)
        count.store(0, std::memory_order_relaxed);
    for (auto& count : feature.steps)
        count.store(0, std::memory_order_relaxed);
    feature.faults.store(0, std::memory_order_relaxed);
    feature.mixedReads.store(0, std::memory_order_relaxed);
    void* target = reinterpret_cast<void*>(builder);
    const auto created = MH_CreateHook(target, reinterpret_cast<void*>(&Halo4TrackerBuilderDetour), &feature.original);
    if (created != MH_OK)
    {
        feature.original = nullptr;
        LOG("TRACKER HEAD Halo 4: StockFallback: hook creation failed (%d)", static_cast<int>(created));
        return false;
    }
    feature.target = target;
    const auto enabled = MH_EnableHook(target);
    if (enabled != MH_OK)
    {
        LOG("TRACKER HEAD Halo 4: StockFallback: hook enable failed (%d)", static_cast<int>(enabled));
        (void)RemoveHalo4HeadRelative();
        return false;
    }
    feature.enabled.store(true, std::memory_order_release);
    LOG("TRACKER HEAD Halo 4: Installed (RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED): the tracker builder halo4.dll+0x%llX "
        "is hooked; its eleventh argument is the heading the player-centred build +0x%llX makes from [engine TLS "
        "+0x%X] + user * 0x%X + 0x%X plus pi/2 (the call returning to +0x%llX; the other build +0x%llX is forwarded "
        "untouched); the component's user is at state%+d. UNPROVEN until that argument has agreed with the aim yaw "
        "this mod's direct drive writes on %u samples across %.0f degrees: until then nothing is changed; switch=%d "
        "sign=%d offset=%.1f deg",
        static_cast<unsigned long long>(sites.builderRva), static_cast<unsigned long long>(sites.playerBuildRva),
        sites.tlsBlock, sites.stride, sites.yawOffset, static_cast<unsigned long long>(sites.returnRva),
        static_cast<unsigned long long>(sites.otherBuildRva), sites.userFromState, kProofSamples,
        kProofSpreadRad * head_relative::kRadToDeg, g_config.tracker_follows_head ? 1 : 0,
        g_config.tracker_head_sign < 0 ? -1 : 1, g_config.tracker_head_offset_deg);
    return true;
}

const char* Halo4TrackerUseName(size_t use)
{
    return halo4_head_relative::TrackerUseName(static_cast<halo4_head_relative::TrackerUse>(use));
}
// The worker's report (every 2 s). Every build: the proof passing or being withdrawn, the switch changing, the
// first use and the first refusal after an install. Dev builds: the window's counts.
void ReportHalo4HeadRelative()
{
    using namespace halo4_head_relative;
    auto& feature = g_halo4TrackerHead;
    if (!feature.target)
        return;
    const uint32_t provenCount = feature.provenCount.load(std::memory_order_relaxed);
    const uint32_t withdrawnCount = feature.withdrawnCount.load(std::memory_order_relaxed);
    if (provenCount != feature.reportedProven)
    {
        feature.reportedProven = provenCount;
        LOG("TRACKER HEAD Halo 4: PROVEN at run time: the builder's eleventh argument agreed with the aim yaw this "
            "mod's direct drive writes (last: the game's %.1f deg, written %.1f deg) on %u samples while that aim "
            "moved across at least %.0f degrees; the tracker may now be handed the head's heading",
            feature.lastNativeDeg.load(std::memory_order_relaxed),
            feature.lastReferenceDeg.load(std::memory_order_relaxed), kProofSamples,
            kProofSpreadRad * head_relative::kRadToDeg);
    }
    if (withdrawnCount != feature.reportedWithdrawn)
    {
        feature.reportedWithdrawn = withdrawnCount;
        LOG("TRACKER HEAD Halo 4: PROOF WITHDRAWN: the argument stopped agreeing with the aim yaw this mod's direct "
            "drive writes (%u disagreements in a row; last: the game's %.1f deg, written %.1f deg); the game's own "
            "tracker until it is proven again",
            kProofWithdrawRun, feature.lastNativeDeg.load(std::memory_order_relaxed),
            feature.lastReferenceDeg.load(std::memory_order_relaxed));
    }
    const int switchNow = g_config.tracker_follows_head ? 1 : 0;
    if (feature.reportedSwitch != switchNow)
    {
        if (feature.reportedSwitch >= 0)
            LOG("TRACKER HEAD Halo 4: switch %s: the tracker now turns with %s", switchNow ? "on" : "off",
                switchNow ? "your head (once proven)" : "the game's own heading (the aim)");
        feature.reportedSwitch = switchNow;
    }
    uint32_t uses[kHalo4TrackerUseCount]{}, steps[kHalo4ProofStepCount]{}, refused = 0;
    for (size_t i = 0; i < kHalo4TrackerUseCount; ++i)
    {
        uses[i] = feature.uses[i].exchange(0, std::memory_order_relaxed);
        if (i != static_cast<size_t>(TrackerUse::Used))
            refused += uses[i];
    }
    for (size_t i = 0; i < kHalo4ProofStepCount; ++i)
        steps[i] = feature.steps[i].exchange(0, std::memory_order_relaxed);
    const uint32_t used = uses[static_cast<size_t>(TrackerUse::Used)];
    const uint32_t faults = feature.faults.exchange(0, std::memory_order_relaxed);
    const uint32_t mixed = feature.mixedReads.exchange(0, std::memory_order_relaxed);
    if (!used && !refused && !faults)
        return;
    const uintptr_t otherReturn = feature.otherReturn.load(std::memory_order_relaxed);
    if (otherReturn && !feature.reportedOtherBuild)
    {
        feature.reportedOtherBuild = true;
        LOG("TRACKER HEAD Halo 4: the builder was also called from another place (returning to halo4.dll+0x%llX): "
            "that call is forwarded untouched, so a tracker it draws is the game's own",
            static_cast<unsigned long long>(otherReturn - feature.base));
    }
    char reasons[320];
    int length = 0;
    reasons[0] = 0;
    for (size_t i = 0; i < kHalo4TrackerUseCount; ++i)
    {
        if (i == static_cast<size_t>(TrackerUse::Used) || !uses[i] || length >= static_cast<int>(sizeof(reasons)) - 48)
            continue;
        const int wrote = _snprintf_s(reasons + length, sizeof(reasons) - length, _TRUNCATE, "%s%s=%u",
                                      length ? ", " : "", Halo4TrackerUseName(i), uses[i]);
        if (wrote > 0)
            length += wrote;
    }
    if (used && !feature.reportedFirstUse)
    {
        feature.reportedFirstUse = true;
        LOG("TRACKER HEAD Halo 4: following the head: first contacts placed by the head's heading (the game's "
            "heading was %.1f deg, the head's %.1f deg)",
            feature.lastNativeDeg.load(std::memory_order_relaxed),
            feature.lastHeadDeg.load(std::memory_order_relaxed));
    }
    if (!used && !feature.reportedFirstRefusal)
    {
        feature.reportedFirstRefusal = true;
        LOG("TRACKER HEAD Halo 4: the game's own tracker for now: %s%s", length ? reasons : "a fault",
            faults && length ? ", and faults" : "");
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    LOG("TRACKER HEAD Halo 4 (dev): substituted %u; the game's heading %.1f deg, the aim the direct drive wrote "
        "%.1f deg, the head's %.1f deg (last); proof %s: agreed %u, lagging %u, disagreed %u, no reference %u; refused %u: %s; "
        "faults %u, mixed sample reads %u; other calls last returned to +0x%llX; built on thread %u, the head "
        "published on thread %u; switch=%d sign=%d offset=%.1f deg",
        used, feature.lastNativeDeg.load(std::memory_order_relaxed),
        feature.lastReferenceDeg.load(std::memory_order_relaxed), feature.lastHeadDeg.load(std::memory_order_relaxed),
        feature.proven.load(std::memory_order_acquire) ? "PASSED" : "pending",
        steps[static_cast<size_t>(ProofStep::Agreed)] + steps[static_cast<size_t>(ProofStep::Proven)],
        steps[static_cast<size_t>(ProofStep::Lagging)],
        steps[static_cast<size_t>(ProofStep::Disagreed)] + steps[static_cast<size_t>(ProofStep::Withdrawn)],
        steps[static_cast<size_t>(ProofStep::NoReference)], refused, length ? reasons : "none", faults, mixed,
        static_cast<unsigned long long>(otherReturn ? otherReturn - feature.base : 0),
        feature.lastThread.load(std::memory_order_relaxed),
        g_halo4HeadPublication.thread.load(std::memory_order_relaxed), switchNow,
        g_config.tracker_head_sign < 0 ? -1 : 1, g_config.tracker_head_offset_deg);
#else
    (void)mixed;
    (void)steps;
#endif
}
