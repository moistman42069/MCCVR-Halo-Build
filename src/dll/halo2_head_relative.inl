// Included by halo2_observer_6dof.cpp inside its anonymous namespace, after halo2_direct_drive.inl (it reads the
// drive's state: g_halo2Drive) and halo2_director_perspective.inl. Installed with the observer core (InstallCore),
// retired by the live RemoveCore (halo2_observer_cleanup.inl), reported from Halo2Observer6Dof_Poll.
//
// HALO 2: THE MOTION TRACKER FOLLOWS THE HEAD -- the runtime half. The sites, the pure logic and the reasoning are
// in src/common/halo2_head_relative_logic.h; the write-up is docs/HALO2-HEAD-RELATIVE-2026-10-07.md.
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H2EK-VERIFIED. No Halo 2 editing kit was available: every binding here
// was read from retail halo2.dll alone, and the names are ours. The lane therefore (a) binds nothing unless every
// static link holds on the loaded image, and (b) changes nothing until the float it means to replace has agreed,
// at run time, with the yaw this mod's own direct drive is writing into the game (ArgumentProof; the reference is
// Halo2TrackerAimReference below). Until then every call is forwarded untouched and counted as "unproven".
//
// In short: once a game tick the motion sensor's sampler asks a small accessor for a POINTER to player control's
// desired angles -- the record the direct drive writes the HAND into -- reads the yaw through it and places every
// contact with it. This lane hooks the accessor and, for that one call and local user 0, hands back a pointer to
// a private pair that carries the head's yaw. Every other caller of the accessor gets the game's own pointer. It
// writes no game memory at all.
//
//   * THE HEAD is the observer's own publication (E-H2-6): ApplyHeadPose commits the tracked camera to the
//     observer result -- the camera both renderers consume -- and publishes it stamped on the sight clock with the
//     title generation. This lane reads `tracked` from it; it adds no publication and touches nothing on the
//     camera path. The sampler runs on the simulation thread, so the sample is checked for generation and age
//     (100 ms) at every use.
//   * EVERY REFUSAL IS THE GAME'S OWN POINTER, counted by reason: not local user 0, unproven, the switch off, the
//     control update not running (the game's own sign that the director holds the player: a cinematic), no /
//     stale / foreign sample, no heading, an overlapping call. Identity and freshness only.
//   * THE PROOF AND THE HELD HEADING ARE ONE CALL'S AT A TIME (`inside`, tried and never waited for). By the call
//     chain the sampler runs on the one thread that runs the game's ticks; that is inferred from the retail
//     binary, not confirmed in game. Halo 4's tracker build runs on three job threads, so this lane does not
//     rest on the reading.
//   * LTCG: `py -3 tools/audit_caller_area.py halo2.dll 6C3940:8:i` is clean (a leaf with no unwind entry that
//     touches no stack; four native callers, none keeps a volatile register across the call), so a plain C++
//     detour forwards through CallNativeWithSpareSlots.
// Cost per call of the sampler's (once a tick): one flag exchange, one clock read, two tick-count reads, one
// seqlock copy of the publication, a dozen float operations. Every other caller: one compare. No allocation, no
// logging, no waiting.
static_assert(kOwnedUser == 0, "halo2_head_relative::TrackerYaw names local user 0");

using Halo2TrackerAnglesFn = float* (__fastcall*)(int32_t user);
constexpr size_t kHalo2TrackerUseCount = static_cast<size_t>(halo2_head_relative::TrackerUse::Count);
constexpr size_t kHalo2TrackerStepCount = static_cast<size_t>(halo2_head_relative::ProofStep::Count);
// The drive's last on-foot write counts as the reference for this long (the write runs every control update).
// (A threshold of ours: it locates nothing in the game.)
constexpr uint64_t kHalo2TrackerReferenceMs = 100;

struct Halo2TrackerHeadRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    void* target = nullptr;
    void* original = nullptr;        // Halo2TrackerAnglesFn
    uintptr_t trackerReturn = 0;     // where the sampler's call of the accessor returns: the one call changed
    // The record as the accessor's own bytes give it (and as the direct drive bound it): for the fallback below.
    uintptr_t recordSlot = 0;
    uint32_t recordStride = 0, yawOffset = 0;
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> callbacks{0};
    // The window the report drains: every call of the sampler's lands in exactly one use.
    std::atomic<uint32_t> uses[kHalo2TrackerUseCount]{};
    std::atomic<uint32_t> steps[kHalo2TrackerStepCount]{};
    std::atomic<uint32_t> faults{0};
    std::atomic<float> lastNativeDeg{0.0f}, lastHeadDeg{0.0f}, lastReferenceDeg{0.0f};
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Dev: the head's heading whenever a usable sample was read (not only on a substitution), how old the last
    // sample read was (usable or stale), how many of user 0's calls found the control update running (these show
    // before the proof has passed, when every refusal reads "unproven"), the sampler's thread and how often it
    // was not the thread of the call before, and the calls of the accessor by its other callers (never changed).
    std::atomic<float> seenHeadDeg{0.0f};
    std::atomic<uint32_t> seenHead{0}, sampleAgeMs{0}, controlLive{0}, lastThread{0}, threadChanges{0}, otherCalls{0};
#endif
    // The proof and the held heading belong to user 0's call of the sampler. By the call chain that is one thread,
    // the one that runs the game's ticks, one call at a time; that is inferred, not confirmed in game (Halo 4's
    // tracker build runs on job threads). `inside` makes "one at a time" a fact instead of a
    // reading of the bytes: a call for user 0 that arrives while another is inside is forwarded untouched and
    // counted (TrackerUse::Overlapped). It is tried, never waited for. `proven`, `provenCount` and
    // `withdrawnCount` are for the report.
    std::atomic<bool> inside{false};
    // The game wrote through the private copy (halo2_head_relative_logic.h, THE PRIVATE COPY): latched for this
    // install; `shadowWrites` counts the calls that found it.
    std::atomic<bool> shadowWritten{false};
    std::atomic<uint32_t> shadowWrites{0};
    size_t shadowBytes = 8;   // the worker, at install; read by the detour
    halo2_head_relative::ArgumentProof proof;  // under `inside`
    std::atomic<bool> proven{false};
    std::atomic<uint32_t> provenCount{0}, withdrawnCount{0};
    head_relative::HeadingHold hold;           // under `inside`
    // The report's thread only.
    uint64_t reportMs = 0;
    int reportedSwitch = -1;
    uint32_t reportedProven = 0, reportedWithdrawn = 0;
    bool reportedFirstUse = false, reportedFirstRefusal = false;
} g_halo2TrackerHead;

// What the sampler reads in place of the record: {the head's yaw, the record's own pitch, and the record's next
// bytes as they are}. Per thread: the sampler reads it at once, on the thread that asked. `Handed` is what was
// put there, to notice a write through the pointer at the next call.
thread_local float t_halo2TrackerAngles[halo2_head_relative::kShadowBytes / sizeof(float)]{};
thread_local float t_halo2TrackerHanded[halo2_head_relative::kShadowBytes / sizeof(float)]{};
thread_local size_t t_halo2TrackerHandedBytes = 0;

// THE PROOF'S REFERENCE: the yaw this mod's direct drive last WROTE into player control, on foot. Only while the
// drive is armed on its own validated record and that write is fresh: then the mod itself put this yaw where the
// tracker is about to read, and the float behind the accessor's pointer can be held against it. A seat is no
// reference: the drive stamps a seat's axes apart (liveMs is an on-foot write's), and a seat's record is not
// always in world angles.
bool Halo2TrackerAimReference(float& yaw) noexcept
{
    auto& st = g_halo2Drive;
    if (!g_config.aim_direct_drive || !st.installed.load(std::memory_order_acquire) ||
        !st.armed.load(std::memory_order_acquire))
        return false;
    const uint64_t live = st.liveMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    if (!live || now < live || now - live > kHalo2TrackerReferenceMs)
        return false;
    if (st.seatYawMs.load(std::memory_order_acquire) || st.seatPitchMs.load(std::memory_order_acquire))
        return false;
    const float wrote = st.wroteYaw.load(std::memory_order_relaxed);
    if (st.liveMs.load(std::memory_order_acquire) != live || !std::isfinite(wrote))
        return false;
    yaw = wrote;
    return true;
}

// The sampler's thread, for the sampler's own call. Decides the pointer it is handed: the game's own (`native`) or
// the private pair. No allocation, no logging, no waiting (one flag that is tried, never waited for), no write to
// game memory.
__declspec(noinline) float* Halo2TrackerAnglesFor(float* native, int32_t user) noexcept
{
    using namespace halo2_head_relative;
    auto& feature = g_halo2TrackerHead;
    TrackerUse use = TrackerUse::NoSample;
    float* handed = native;
    volatile bool holding = false;  // this call is user 0's and holds `inside` (the handler reads it too)
    __try
    {
        const float nativeYaw = native[0];
        // Did the game write through the copy it was handed at this thread's last call? Then never again.
        if (t_halo2TrackerHandedBytes)
        {
            if (ShadowWritten(t_halo2TrackerAngles, t_halo2TrackerHanded, t_halo2TrackerHandedBytes))
            {
                feature.shadowWrites.fetch_add(1, std::memory_order_relaxed);
                feature.shadowWritten.store(true, std::memory_order_release);
            }
            t_halo2TrackerHandedBytes = 0;
        }
        TrackerRequest request;
        request.user = user;
        head_relative::HeadSample sample;
        if (user == 0)
        {
            if (feature.inside.exchange(true, std::memory_order_acquire))
                use = TrackerUse::Overlapped;
            else
            {
                holding = true;
                // The proof runs whatever the switch says: it is about the game, not about the player's choice.
                float reference = 0.0f;
                const bool usable = Halo2TrackerAimReference(reference);
                const ProofStep step = StepProof(feature.proof, nativeYaw, usable, reference);
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
                feature.lastNativeDeg.store(WrapPi(nativeYaw) * head_relative::kRadToDeg, std::memory_order_relaxed);
                // The head, as the observer last committed it (a coherent copy, or nothing).
                Halo2ObserverPosePublication publication{};
                if (Halo2Observer6Dof_ReadPublishedPose(publication))
                {
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        sample.forward[axis] = publication.tracked.forward[axis];
                        sample.up[axis] = publication.tracked.up[axis];
                    }
                    sample.stampUs = publication.publishedUs;
                    sample.generation = publication.generation;
                    sample.valid = publication.snapshot.valid;
                }
            }
        }
        if (use != TrackerUse::Overlapped)
        {
            // Only user 0's call, holding `inside`, reads the proof or reaches the held heading: TrackerYaw
            // returns before `hold` for every other user.
            request.proven = holding && feature.proof.proven;
            request.switchOn = g_config.tracker_follows_head;
            request.controlFresh =
                ControlFresh(g_halo2Drive.controlMs.load(std::memory_order_acquire), GetTickCount64());
            request.sign = g_config.tracker_head_sign;
            request.offsetDeg = g_config.tracker_head_offset_deg;
            request.nowUs = Sight_NowUs();
            // This hook belongs to one load of the title; a publication from another is not this tracker's head.
            const uint32_t generation = g_generation.load(std::memory_order_acquire);
            request.generation = generation == feature.generation ? generation : 0;
            float replaced = nativeYaw, headYaw = 0.0f;
            use = TrackerYaw(request, sample, feature.hold, nativeYaw, replaced, &headYaw);
            if (use == TrackerUse::Used && feature.shadowWritten.load(std::memory_order_acquire))
                use = TrackerUse::WrittenThrough;
            if (use == TrackerUse::Used)
            {
                const size_t bytes = feature.shadowBytes;
                FillShadow(t_halo2TrackerAngles, t_halo2TrackerHanded, native, bytes, replaced);
                t_halo2TrackerHandedBytes = bytes;
                handed = t_halo2TrackerAngles;
                feature.lastHeadDeg.store(headYaw * head_relative::kRadToDeg, std::memory_order_relaxed);
            }
#if HALOMCCVR_DEV_DIAGNOSTICS
            // Dev: the head's heading beside the game's on every call that had a usable sample, substituted or not
            // (so the two can be read side by side before the proof has passed), and the age of the
            // last sample of this generation that was read, usable or stale (a slow frame rate refuses on age
            // alone, and then the age is the thing to see).
            if (holding)
            {
                const head_relative::Use sampled =
                    head_relative::SampleUse(sample, request.generation, request.nowUs);
                if (sampled == head_relative::Use::Ok || sampled == head_relative::Use::Stale)
                    feature.sampleAgeMs.store(static_cast<uint32_t>((request.nowUs - sample.stampUs) / 1000),
                                              std::memory_order_relaxed);
                float seen[3];
                if (sampled == head_relative::Use::Ok && head_relative::Heading(sample.forward, kWorldUp, seen))
                {
                    feature.seenHeadDeg.store(std::atan2(seen[1], seen[0]) * head_relative::kRadToDeg,
                                              std::memory_order_relaxed);
                    feature.seenHead.fetch_add(1, std::memory_order_relaxed);
                }
                if (request.controlFresh)
                    feature.controlLive.fetch_add(1, std::memory_order_relaxed);
            }
#endif
        }
#if HALOMCCVR_DEV_DIAGNOSTICS
        // Dev: every thread the sampler was seen on is worth knowing; the report prints the last and says when
        // it changed inside a window.
        const uint32_t thread = GetCurrentThreadId();
        const uint32_t before = feature.lastThread.exchange(thread, std::memory_order_relaxed);
        if (before && before != thread)
            feature.threadChanges.fetch_add(1, std::memory_order_relaxed);
#endif
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (holding)
            feature.inside.store(false, std::memory_order_release);
        feature.faults.fetch_add(1, std::memory_order_relaxed);
        return native;
    }
    if (holding)
        feature.inside.store(false, std::memory_order_release);
    feature.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
    return handed;
}

// A detour entered with no trampoline to forward to cannot happen while the hook is enabled. If it ever did, the
// caller would still dereference what comes back, so this is the accessor's own arithmetic on the record decoded
// at install: the pointer the game's 25 bytes would have returned.
__declspec(noinline) float* Halo2TrackerAnglesFallback(int32_t user) noexcept
{
    const auto& feature = g_halo2TrackerHead;
    if (!feature.recordSlot)
        return nullptr;
    __try
    {
        const uintptr_t globals = *reinterpret_cast<const uintptr_t*>(feature.recordSlot);
        return reinterpret_cast<float*>(globals + feature.yawOffset +
                                        static_cast<uintptr_t>(static_cast<int64_t>(user)) * feature.recordStride);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

__declspec(noinline) float* __fastcall Halo2TrackerAnglesDetour(int32_t user)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto& feature = g_halo2TrackerHead;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    float* angles = nullptr;
    __try
    {
        const auto original = reinterpret_cast<Halo2TrackerAnglesFn>(feature.original);
        angles = original ? CallNativeWithSpareSlots(original, user) : Halo2TrackerAnglesFallback(user);
        // Only the sampler's own call is ever changed: the accessor's three other callers (the HUD's sway, the
        // following camera's update, one not identified) take the game's own pointer.
        if (caller == feature.trackerReturn)
        {
            if (angles && feature.enabled.load(std::memory_order_acquire))
                angles = Halo2TrackerAnglesFor(angles, user);
#if HALOMCCVR_DEV_DIAGNOSTICS
            // Dev: the sound listener witness samples here, once a game tick (halo2_sound_listener_witness.inl).
            // It reads only, and it does not touch `angles`.
            Halo2ListenerWitnessTick(user);
#endif
        }
#if HALOMCCVR_DEV_DIAGNOSTICS
        else
            feature.otherCalls.fetch_add(1, std::memory_order_relaxed);
#endif
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
    return angles;
}

// The game's bytes at a fixed place, as they stood before any hook of ours. False = unreadable.
__declspec(noinline) bool Halo2HeadRelativeCopy(uintptr_t address, void* out, size_t size) noexcept
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
bool Halo2HeadRelativeRead(uintptr_t base, size_t size, uintptr_t address, void* out, size_t bytes)
{
    return address && address >= base && bytes <= size && address - base <= size - bytes &&
           Halo2HeadRelativeCopy(address, out, bytes);
}
// A pattern's one match in the image, or 0 when it is missing or matches twice.
uintptr_t Halo2HeadRelativeUnique(uintptr_t base, size_t size, const char* pattern)
{
    const uintptr_t match = sig::Find(base, size, pattern);
    if (!match || match + 1 >= base + size)
        return 0;
    return sig::Find(match + 1, base + size - match - 1, pattern) ? 0 : match;
}
// `function`(x) against the C runtime's own sine or cosine at two angles. Guarded: `function` is a pointer read
// from the title's import table.
__declspec(noinline) bool Halo2TrackerTrigAnswers(double (*function)(double), bool sine) noexcept
{
    const double angles[2]{0.5, -2.25};
    __try
    {
        for (int i = 0; i < 2; ++i)
        {
            const double expected = sine ? std::sin(angles[i]) : std::cos(angles[i]);
            if (!(std::fabs(function(angles[i]) - expected) <= 1.0e-12))
                return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
// The sampler's two trig calls are import thunks (jmp [slot]) whose slots hold the C runtime's sin, then cos.
// Null = they are; otherwise what did not hold. `how` says how they were identified.
const char* Halo2TrackerProveTrig(uintptr_t base, size_t size, const halo2_head_relative::TrackerSites& sites,
                                  const char*& how)
{
    double (*imported[2])(double) = {nullptr, nullptr};
    const int64_t thunks[2]{sites.sinRva, sites.cosRva};
    for (int i = 0; i < 2; ++i)
    {
        uint8_t thunk[6]{};
        const uintptr_t at = thunks[i] > 0 ? base + static_cast<uintptr_t>(thunks[i]) : 0;
        if (!Halo2HeadRelativeRead(base, size, at, thunk, sizeof(thunk)) || thunk[0] != 0xFF || thunk[1] != 0x25)
            return "the sampler's trig calls are not import thunks";
        const uintptr_t slot = at + 6 + static_cast<uintptr_t>(static_cast<int64_t>(halo2_head_relative::ReadI32(thunk + 2)));
        uintptr_t function = 0;
        if (!Halo2HeadRelativeRead(base, size, slot, &function, sizeof(function)))
            return "a trig import slot lies outside the module";
        if (!function || (function >= base && function - base < size))
            return "a trig import is not bound";
        imported[i] = reinterpret_cast<double (*)(double)>(function);
    }
    const HMODULE runtime = GetModuleHandleW(L"ucrtbase.dll");
    const void* runtimeSin = runtime ? reinterpret_cast<const void*>(GetProcAddress(runtime, "sin")) : nullptr;
    const void* runtimeCos = runtime ? reinterpret_cast<const void*>(GetProcAddress(runtime, "cos")) : nullptr;
    if (runtimeSin && runtimeCos && reinterpret_cast<const void*>(imported[0]) == runtimeSin &&
        reinterpret_cast<const void*>(imported[1]) == runtimeCos)
        how = "the two trig imports are ucrtbase's sin then cos";
    else if (Halo2TrackerTrigAnswers(imported[0], true) && Halo2TrackerTrigAnswers(imported[1], false))
        how = "the two trig imports answer as sin then cos";
    else
        return "the sampler's trig calls are not sin then cos";
    return nullptr;
}

bool RemoveHalo2HeadRelative() noexcept
{
    auto& feature = g_halo2TrackerHead;
    feature.enabled.store(false, std::memory_order_release);
    if (!feature.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(feature.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("TRACKER HEAD Halo 2: CleanupRequired: disable failed (%d)", static_cast<int>(disabled));
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo2TrackerAnglesDetour)};
    const void* trampolines[]{feature.original};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, feature.callbacks))
    {
        LOG("TRACKER HEAD Halo 2: CleanupRequired: the detour is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(feature.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("TRACKER HEAD Halo 2: CleanupRequired: removal failed (%d)", static_cast<int>(removed));
        return false;
    }
    feature.target = nullptr;
    feature.original = nullptr;
    feature.trackerReturn = 0;
    return true;
}

// Management thread, with the observer's install, after the direct drive. Binds nothing unless every static link
// holds on this image; a refusal is the game's own tracker, said once.
bool InstallHalo2HeadRelative(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace halo2_head_relative;
    auto& feature = g_halo2TrackerHead;
    if (feature.target)
    {
        LOG("TRACKER HEAD Halo 2: StockFallback: the previous install was not retired");
        return false;
    }
    if (!base || !generation || size < 0x1000)
    {
        LOG("TRACKER HEAD Halo 2: StockFallback: no title image or generation");
        return false;
    }
    // The direct drive is both this lane's cross-check (the record it bound from the accumulator) and its run-time
    // reference (the yaw it writes). Without its hook there is neither.
    const auto& drive = g_halo2Drive;
    if (!drive.installed.load(std::memory_order_acquire) || !drive.slot || drive.slot < base || drive.slot - base >= size)
    {
        LOG("TRACKER HEAD Halo 2: StockFallback: the direct drive's control hook is not installed (the record it "
            "bound is this lane's cross-check and the yaw it writes is the run-time proof's reference); the game's "
            "own tracker. Retail-derived lead, not H2EK-verified");
        return false;
    }
    const uintptr_t accessor = Halo2HeadRelativeUnique(base, size, kAnglesAccessorPattern);
    const uintptr_t sampler = Halo2HeadRelativeUnique(base, size, kTrackerSamplerPattern);
    const uintptr_t placement = Halo2HeadRelativeUnique(base, size, kTrackerPlacementPattern);
    const uintptr_t loop = Halo2HeadRelativeUnique(base, size, kTrackerUpdateLoopPattern);
    uint8_t accessorBytes[kAccessorBytes]{}, samplerBytes[kSamplerBytes]{}, placementBytes[kPlacementBytes]{},
        loopBytes[kLoopBytes]{};
    uint8_t halfPiBytes[sizeof(float)]{}, maskBytes[sizeof(uint32_t)]{};
    const bool accessorRead = Halo2HeadRelativeRead(base, size, accessor, accessorBytes, kAccessorBytes);
    // The sampler's match must begin a function of the image's own function table.
    DWORD64 imageBase = 0;
    const auto* samplerFunction = sampler ? RtlLookupFunctionEntry(sampler, &imageBase, nullptr) : nullptr;
    const bool samplerRead = Halo2HeadRelativeRead(base, size, sampler, samplerBytes, kSamplerBytes) &&
                             samplerFunction && imageBase == base && samplerFunction->BeginAddress == sampler - base;
    const bool placementRead = Halo2HeadRelativeRead(base, size, placement, placementBytes, kPlacementBytes);
    const bool loopRead = Halo2HeadRelativeRead(base, size, loop, loopBytes, kLoopBytes);
    bool constantsRead = false;
    if (samplerRead)
    {
        const int64_t samplerRva = static_cast<int64_t>(sampler - base);
        const int64_t halfPiRva =
            halo2_head_relative::RipRva(samplerBytes, kSamplerHalfPiDisp, kSamplerHalfPiNext, samplerRva);
        const int64_t maskRva =
            halo2_head_relative::RipRva(samplerBytes, kSamplerMaskDisp, kSamplerMaskNext, samplerRva);
        constantsRead = halfPiRva > 0 && maskRva > 0 &&
                        Halo2HeadRelativeRead(base, size, base + static_cast<uintptr_t>(halfPiRva), halfPiBytes,
                                              sizeof(halfPiBytes)) &&
                        Halo2HeadRelativeRead(base, size, base + static_cast<uintptr_t>(maskRva), maskBytes,
                                              sizeof(maskBytes));
    }
    DriveRecord record;
    record.slotRva = static_cast<int64_t>(drive.slot - base);
    record.stride = static_cast<uint32_t>(kHalo2PcStride);
    record.yawOffset = static_cast<uint32_t>(kHalo2PcDesiredYaw);
    TrackerSites sites;
    const TrackerBind bind = DecodeTrackerSites(
        accessorRead ? accessorBytes : nullptr, static_cast<int64_t>(accessor - base),
        samplerRead ? samplerBytes : nullptr, static_cast<int64_t>(sampler - base),
        placementRead ? placementBytes : nullptr, static_cast<int64_t>(placement - base),
        loopRead ? loopBytes : nullptr, static_cast<int64_t>(loop - base), constantsRead ? halfPiBytes : nullptr,
        constantsRead ? maskBytes : nullptr, record, sites);
    const char* trig = "";
    const char* why = bind != TrackerBind::Bound ? TrackerBindName(bind) : Halo2TrackerProveTrig(base, size, sites, trig);
    if (why)
    {
        LOG("TRACKER HEAD Halo 2: StockFallback: %s (accessor %s, sampler %s, placement %s, update loop %s); the "
            "game's own tracker. Retail-derived lead, not H2EK-verified",
            why, accessor ? "matched" : "unmatched", sampler ? "matched" : "unmatched",
            placement ? "matched" : "unmatched", loop ? "matched" : "unmatched");
        return false;
    }
    feature.base = base;
    feature.generation = generation;
    feature.trackerReturn = base + static_cast<uintptr_t>(sites.returnRva);
    feature.recordSlot = base + static_cast<uintptr_t>(sites.recordSlotRva);
    feature.recordStride = sites.recordStride;
    feature.yawOffset = sites.yawOffset;
    feature.shadowBytes = ShadowBytes(sites.recordStride, sites.yawOffset);
    feature.shadowWritten.store(false, std::memory_order_release);
    feature.shadowWrites.store(0, std::memory_order_relaxed);
    feature.inside.store(false, std::memory_order_release);   // nothing can be inside: the hook is not created yet
    feature.proof = ArgumentProof{};
    feature.proven.store(false, std::memory_order_release);
    feature.provenCount.store(0, std::memory_order_relaxed);
    feature.withdrawnCount.store(0, std::memory_order_relaxed);
    feature.hold = head_relative::HeadingHold{};
    feature.reportMs = 0;
    feature.reportedSwitch = -1;
    feature.reportedProven = feature.reportedWithdrawn = 0;
    feature.reportedFirstUse = feature.reportedFirstRefusal = false;
    for (auto& count : feature.uses)
        count.store(0, std::memory_order_relaxed);
    for (auto& count : feature.steps)
        count.store(0, std::memory_order_relaxed);
    feature.faults.store(0, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    feature.seenHead.store(0, std::memory_order_relaxed);
    feature.controlLive.store(0, std::memory_order_relaxed);
    feature.otherCalls.store(0, std::memory_order_relaxed);
    feature.lastThread.store(0, std::memory_order_relaxed);   // another load may tick on another thread
    feature.threadChanges.store(0, std::memory_order_relaxed);
#endif
    void* const target = reinterpret_cast<void*>(accessor);
    const auto created = MH_CreateHook(target, reinterpret_cast<void*>(&Halo2TrackerAnglesDetour), &feature.original);
    if (created != MH_OK)
    {
        feature.original = nullptr;
        LOG("TRACKER HEAD Halo 2: StockFallback: hook creation failed (%d)", static_cast<int>(created));
        return false;
    }
    feature.target = target;
    feature.enabled.store(true, std::memory_order_release);
    const auto enabled = MH_EnableHook(target);
    if (enabled != MH_OK)
    {
        LOG("TRACKER HEAD Halo 2: StockFallback: hook enable failed (%d)", static_cast<int>(enabled));
        (void)RemoveHalo2HeadRelative();
        return false;
    }
    LOG("TRACKER HEAD Halo 2: Installed (RETAIL-DERIVED LEAD, NOT H2EK-VERIFIED): the desired-angles accessor "
        "halo2.dll+0x%llX is hooked. Once a game tick the motion sensor's sampler +0x%llX (called per user by the "
        "update's loop +0x%llX) reads its user's yaw through that accessor's pointer (the call returning to "
        "+0x%llX) and places every contact with sin / cos of -(yaw + pi/2) (+0x%llX); for that one call and local "
        "user 0 it is handed a private pair with the HEAD's yaw, every other caller keeps the game's own pointer, "
        "and nothing in the game is written. Held statically: four unique signatures, the sampler's call on the "
        "accessor, the loop's on the sampler, pi/2 and the sign mask, %s, and the accessor's record (globals "
        "+0x%llX, stride 0x%X, yaw +0x%X) is the one the direct drive bound. UNPROVEN until the float behind that "
        "pointer has agreed with the yaw this mod's direct drive writes on %u samples, one of them at least %.0f "
        "degrees from the first and not a half turn from it: until then nothing is changed; switch=%d sign=%d "
        "offset=%.1f deg",
        static_cast<unsigned long long>(sites.accessorRva), static_cast<unsigned long long>(sites.samplerRva),
        static_cast<unsigned long long>(sites.updateLoopRva), static_cast<unsigned long long>(sites.returnRva),
        static_cast<unsigned long long>(sites.placementRva), trig,
        static_cast<unsigned long long>(sites.recordSlotRva), sites.recordStride, sites.yawOffset, kProofSamples,
        kProofSpreadRad * head_relative::kRadToDeg, g_config.tracker_follows_head ? 1 : 0,
        g_config.tracker_head_sign < 0 ? -1 : 1, g_config.tracker_head_offset_deg);
    return true;
}

const char* Halo2TrackerUseName(size_t use)
{
    return halo2_head_relative::TrackerUseName(static_cast<halo2_head_relative::TrackerUse>(use));
}
// The title worker's report, every 2 s (its own clock: the observer's telemetry runs every 5). Every build: the
// proof passing or being withdrawn, the switch changing, the first use and the first refusal after an install.
// Dev builds: the window's counts, with the game's heading beside the head's.
void ReportHalo2HeadRelative() noexcept
{
    using namespace halo2_head_relative;
    auto& feature = g_halo2TrackerHead;
    if (!feature.target)
        return;
    const uint64_t now = GetTickCount64();
    if (now - feature.reportMs < 2000)
        return;
    feature.reportMs = now;
    const uint32_t provenCount = feature.provenCount.load(std::memory_order_relaxed);
    const uint32_t withdrawnCount = feature.withdrawnCount.load(std::memory_order_relaxed);
    if (provenCount != feature.reportedProven)
    {
        feature.reportedProven = provenCount;
        LOG("TRACKER HEAD Halo 2: PROVEN at run time: the yaw the motion sensor reads through the accessor agreed "
            "with the yaw this mod's direct drive writes (last: the game's %.1f deg, written %.1f deg) on %u "
            "samples, one of them at least %.0f degrees from the first and not a half turn from it; the tracker "
            "may now be handed the head's heading",
            feature.lastNativeDeg.load(std::memory_order_relaxed),
            feature.lastReferenceDeg.load(std::memory_order_relaxed), kProofSamples,
            kProofSpreadRad * head_relative::kRadToDeg);
    }
    if (withdrawnCount != feature.reportedWithdrawn)
    {
        feature.reportedWithdrawn = withdrawnCount;
        LOG("TRACKER HEAD Halo 2: PROOF WITHDRAWN: the yaw the motion sensor reads stopped agreeing with the yaw "
            "this mod's direct drive writes (%u disagreements in a row; last: the game's %.1f deg, written %.1f "
            "deg); the game's own tracker until it is proven again",
            kProofWithdrawRun, feature.lastNativeDeg.load(std::memory_order_relaxed),
            feature.lastReferenceDeg.load(std::memory_order_relaxed));
    }
    const int switchNow = g_config.tracker_follows_head ? 1 : 0;
    if (feature.reportedSwitch != switchNow)
    {
        if (feature.reportedSwitch >= 0)
            LOG("TRACKER HEAD Halo 2: switch %s: the tracker now turns with %s", switchNow ? "on" : "off",
                switchNow ? "your head (once proven)" : "the game's own heading (the aim)");
        feature.reportedSwitch = switchNow;
    }
    uint32_t uses[kHalo2TrackerUseCount]{}, steps[kHalo2TrackerStepCount]{}, refused = 0;
    for (size_t i = 0; i < kHalo2TrackerUseCount; ++i)
    {
        uses[i] = feature.uses[i].exchange(0, std::memory_order_relaxed);
        if (i != static_cast<size_t>(TrackerUse::Used))
            refused += uses[i];
    }
    for (size_t i = 0; i < kHalo2TrackerStepCount; ++i)
        steps[i] = feature.steps[i].exchange(0, std::memory_order_relaxed);
    const uint32_t used = uses[static_cast<size_t>(TrackerUse::Used)];
    const uint32_t faults = feature.faults.exchange(0, std::memory_order_relaxed);
    if (!used && !refused && !faults)
        return;
    char reasons[320];
    int length = 0;
    reasons[0] = 0;
    for (size_t i = 0; i < kHalo2TrackerUseCount; ++i)
    {
        if (i == static_cast<size_t>(TrackerUse::Used) || !uses[i] || length >= static_cast<int>(sizeof(reasons)) - 48)
            continue;
        const int wrote = _snprintf_s(reasons + length, sizeof(reasons) - length, _TRUNCATE, "%s%s=%u",
                                      length ? ", " : "", Halo2TrackerUseName(i), uses[i]);
        if (wrote > 0)
            length += wrote;
    }
    if (used && !feature.reportedFirstUse)
    {
        feature.reportedFirstUse = true;
        LOG("TRACKER HEAD Halo 2: following the head: first contacts placed by the head's heading (the game's "
            "heading was %.1f deg, the head's %.1f deg)",
            feature.lastNativeDeg.load(std::memory_order_relaxed),
            feature.lastHeadDeg.load(std::memory_order_relaxed));
    }
    if (!used && !feature.reportedFirstRefusal)
    {
        feature.reportedFirstRefusal = true;
        LOG("TRACKER HEAD Halo 2: the game's own tracker for now: %s%s", length ? reasons : "a fault",
            faults && length ? ", and faults" : "");
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    // THE SIDE-BY-SIDE CHECK: the game's heading beside the head's, and their difference. `seen` counts the calls of
    // this window in which a usable head sample was read, and `control` those that found the control update
    // running, substituted or not.
    const uint32_t seen = feature.seenHead.exchange(0, std::memory_order_relaxed);
    const uint32_t control = feature.controlLive.exchange(0, std::memory_order_relaxed);
    const uint32_t others = feature.otherCalls.exchange(0, std::memory_order_relaxed);
    const uint32_t threadChanges = feature.threadChanges.exchange(0, std::memory_order_relaxed);
    const float gameDeg = feature.lastNativeDeg.load(std::memory_order_relaxed);
    const float headDeg = feature.seenHeadDeg.load(std::memory_order_relaxed);
    const float apart = halo2_head_relative::WrapPi((headDeg - gameDeg) * head_relative::kDegToRad) * head_relative::kRadToDeg;
    LOG("TRACKER HEAD Halo 2 (dev): substituted %u; the game's heading %.1f deg, the head's %.1f deg, head minus "
        "game %+.1f deg (last; a head sample was usable on %u calls, the last one read was %u ms old; the control "
        "update was running on %u); the aim the direct drive wrote %.1f deg; proof %s: agreed %u, lagging %u, disagreed %u, "
        "no reference %u; refused %u: %s; faults %u; the accessor's other callers %u calls (untouched); sampled "
        "on thread %u (%u calls came on another thread than the call before); switch=%d sign=%d offset=%.1f deg",
        used, gameDeg, seen ? headDeg : 0.0f, seen ? apart : 0.0f, seen,
        feature.sampleAgeMs.load(std::memory_order_relaxed), control,
        feature.lastReferenceDeg.load(std::memory_order_relaxed),
        feature.proven.load(std::memory_order_acquire) ? "PASSED" : "pending",
        steps[static_cast<size_t>(ProofStep::Agreed)] + steps[static_cast<size_t>(ProofStep::Proven)],
        steps[static_cast<size_t>(ProofStep::Lagging)],
        steps[static_cast<size_t>(ProofStep::Disagreed)] + steps[static_cast<size_t>(ProofStep::Withdrawn)],
        steps[static_cast<size_t>(ProofStep::NoReference)], refused, length ? reasons : "none", faults, others,
        feature.lastThread.load(std::memory_order_relaxed), threadChanges, switchNow,
        g_config.tracker_head_sign < 0 ? -1 : 1, g_config.tracker_head_offset_deg);
#else
    (void)steps;
#endif
}
