// HALO 4: OBJECTIVE MARKERS FOLLOW THE HEAD, NOT THE GUN -- the runtime half. The sites, the proof and the
// reasoning are in src/common/halo4_nav_marker_logic.h.
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED. The hook binds nothing unless both patterns hold on the
// loaded image, and it changes nothing until the camera it is handed has agreed, at run time, with the aim camera
// the stereo transaction published (MarkerProof); until then every call is forwarded untouched and counted as
// "camera not yet proven to be the aim".
//
// In short: the HUD's widget placement (+0x40B5F4) is handed the aim camera between frames. Outside the eye passes
// this hands it, instead, a copy of that camera with the head's pose and a projection the game builds from the copy.
// Nothing of the game's is written: not its camera, its projection or the observer.

// +0x40B5F4. Twelve arguments; tools/audit_caller_area.py (git history) on halo4.dll 40B5F4:16:i is clean: it
// reads its caller's area up to entry+0x60 (the twelfth argument) and none of its eleven native callers keeps a
// volatile register across the call. Arguments two and five to twelve are passed through as raw slots (the sixth
// and eighth are floats on the stack: the slot carries their bits unchanged); the answer is in al and goes back
// whole.
using Halo4MarkerProjectorFn = uint64_t (__fastcall*)(void* point, uint64_t clamp, void* camera, void* projection,
                                                      uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9,
                                                      uint64_t a10, uint64_t a11, uint64_t a12);
// +0x38F658: the game's projection from a render camera (camera, bounds or 0, projection out, a float).
using Halo4ProjectionBuilderFn = void (__fastcall*)(void* camera, uint64_t bounds, void* projection, float extra);

constexpr size_t kHalo4NavUseCount = static_cast<size_t>(halo4_nav_markers::Use::Count);
constexpr size_t kHalo4NavProofCount = static_cast<size_t>(halo4_nav_markers::ProofStep::Count);
struct Halo4NavMarkersRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    void* target = nullptr;
    void* original = nullptr;                         // Halo4MarkerProjectorFn
    Halo4ProjectionBuilderFn builder = nullptr;
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> callbacks{0};
    // The proof has one writer at a time: whoever holds `proving`. A call that finds it held does not step it.
    std::atomic<bool> proving{false};
    halo4_nav_markers::MarkerProof proof;
    std::atomic<bool> proven{false};
    std::atomic<uint32_t> provenCount{0}, withdrawnCount{0};
    // The window the report drains: every call lands in exactly one verdict, and every proof step is counted.
    std::atomic<uint32_t> uses[kHalo4NavUseCount]{};
    std::atomic<uint32_t> steps[kHalo4NavProofCount]{};
    std::atomic<float> lastTurnDeg{0.0f};  // the last call given the head: the head's forward from the camera's
    // The report's thread only.
    uint32_t reportedProven = 0, reportedWithdrawn = 0;
    bool reportedFirstUse = false;
} g_halo4NavMarkers;

// One call, outside the detour's own frame so a fault in it is caught here. Fills `camera` and `projection` (the
// copies handed to the game) only for Used.
__declspec(noinline) halo4_nav_markers::Use Halo4NavMarkerHead(const void* gameCamera, const void* gameProjection,
                                                               uint8_t* camera, uint8_t* projection) noexcept
{
    using namespace halo4_nav_markers;
    auto& feature = g_halo4NavMarkers;
    Use use = Use::Fault;
    __try
    {
        const bool inEyePass = g_halo4RigTrackingActive;
        halo4_sound_listener::HeadFrame frame;
        bool gameplay = false;
        const bool read = !inEyePass && Halo4ReadHeadFrame(frame, gameplay);
        float handed[3];
        memcpy(handed, static_cast<const uint8_t*>(gameCamera) + kCameraForward, sizeof(handed));
        if (!inEyePass && !feature.proving.exchange(true, std::memory_order_acquire))
        {
            const ProofStep step = StepProof(feature.proof, handed, read && gameplay && frame.sample.valid,
                                             frame.aimForward);
            feature.steps[static_cast<size_t>(step)].fetch_add(1, std::memory_order_relaxed);
            if (step == ProofStep::Proven)
                feature.provenCount.fetch_add(1, std::memory_order_relaxed);
            else if (step == ProofStep::Withdrawn)
                feature.withdrawnCount.fetch_add(1, std::memory_order_relaxed);
            feature.proven.store(feature.proof.proven, std::memory_order_release);
            feature.proving.store(false, std::memory_order_release);
        }
        const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
        use = HeadUse(g_config.nav_markers_follow_head, inEyePass, read, gameplay, frame.sample, frame.position,
                      generation == feature.generation ? generation : 0, Sight_NowUs());
        if (use == Use::Used && !feature.proven.load(std::memory_order_acquire))
            use = Use::Unproven;
        if (use == Use::Used)
        {
            memcpy(camera, gameCamera, kCameraBytes);
            memcpy(projection, gameProjection, kProjectionBytes);
            // Part 1's switch: the vertical field of view the eyes were last drawn with, the one field the stereo
            // transaction widens (0 keeps the game's).
            const float fov = g_config.nav_markers_on_target
                                  ? g_halo4Camera.lastElementVerticalFov.load(std::memory_order_relaxed)
                                  : 0.0f;
            WriteHeadCamera(camera, frame.position, frame.sample.forward, frame.sample.up, fov);
            feature.builder(camera, 0, projection, 0.0f);
            float c = handed[0] * frame.sample.forward[0] + handed[1] * frame.sample.forward[1] +
                      handed[2] * frame.sample.forward[2];
            c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
            feature.lastTurnDeg.store(acosf(c) * head_relative::kRadToDeg, std::memory_order_relaxed);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        use = Use::Fault;
    }
    feature.uses[static_cast<size_t>(use)].fetch_add(1, std::memory_order_relaxed);
    return use;
}

__declspec(noinline) uint64_t __fastcall Halo4MarkerProjectorDetour(void* point, uint64_t clamp, void* camera,
                                                                   void* projection, uint64_t a5, uint64_t a6,
                                                                   uint64_t a7, uint64_t a8, uint64_t a9,
                                                                   uint64_t a10, uint64_t a11, uint64_t a12)
{
    auto& feature = g_halo4NavMarkers;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        const auto original = reinterpret_cast<Halo4MarkerProjectorFn>(feature.original);
        if (!original)
            return 0;
        alignas(16) uint8_t headCamera[halo4_nav_markers::kCameraBytes];
        alignas(16) uint8_t headProjection[halo4_nav_markers::kProjectionBytes];
        void* useCamera = camera;
        void* useProjection = projection;
        if (camera && projection && feature.enabled.load(std::memory_order_acquire) &&
            Halo4NavMarkerHead(camera, projection, headCamera, headProjection) == halo4_nav_markers::Use::Used)
        {
            useCamera = headCamera;
            useProjection = headProjection;
        }
        return CallNativeWithSpareSlots(original, point, clamp, useCamera, useProjection, a5, a6, a7, a8, a9, a10,
                                        a11, a12);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}

bool RemoveHalo4NavMarkers()
{
    auto& feature = g_halo4NavMarkers;
    feature.enabled.store(false, std::memory_order_release);
    if (!feature.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(feature.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
    {
        LOG("NAV MARKERS Halo 4: CleanupRequired: disable failed (%d)", static_cast<int>(disabled));
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo4MarkerProjectorDetour)};
    const void* trampolines[]{feature.original};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, feature.callbacks))
    {
        LOG("NAV MARKERS Halo 4: CleanupRequired: the detour is busy");
        return false;
    }
    const auto removed = MH_RemoveHook(feature.target);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
    {
        LOG("NAV MARKERS Halo 4: CleanupRequired: removal failed (%d)", static_cast<int>(removed));
        return false;
    }
    feature.target = nullptr;
    feature.original = nullptr;
    feature.builder = nullptr;
    return true;
}
// Cold, on the title's install thread, after the camera core. Binds nothing unless both patterns hold on this image;
// a refusal is the game's own marker placement, said once.
bool InstallHalo4NavMarkers(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace halo4_nav_markers;
    auto& feature = g_halo4NavMarkers;
    if (feature.target)
    {
        LOG("NAV MARKERS Halo 4: StockFallback: the previous install was not retired");
        return false;
    }
    if (!base || !generation || size < 0x1000)
    {
        LOG("NAV MARKERS Halo 4: StockFallback: no title image or generation");
        return false;
    }
    const uintptr_t projector = Halo4HeadRelativeUnique(base, size, kProjectorPattern);
    const uintptr_t build = Halo4HeadRelativeUnique(base, size, kCameraBuildPattern);
    uint8_t projectorBytes[kProjectorBytes]{}, buildBytes[kCameraBuildBytes]{};
    const bool projectorRead = Halo4HeadRelativeRead(base, size, projector, projectorBytes, kProjectorBytes);
    const bool buildRead = Halo4HeadRelativeRead(base, size, build, buildBytes, kCameraBuildBytes);
    Sites sites;
    const Bind bind = DecodeSites(
        projectorRead ? projectorBytes : nullptr, static_cast<int64_t>(projector - base),
        projectorRead && Halo4HeadRelativeFunctionRva(base, projector) == static_cast<int64_t>(projector - base),
        buildRead ? buildBytes : nullptr, static_cast<int64_t>(build - base),
        buildRead && Halo4HeadRelativeFunctionRva(base, build) == static_cast<int64_t>(build - base),
        [base, size](int64_t rva) {
            return rva > 0 && static_cast<uint64_t>(rva) < size &&
                   Halo4HeadRelativeFunctionRva(base, base + static_cast<uintptr_t>(rva)) == rva;
        },
        sites);
    if (bind != Bind::Bound)
    {
        LOG("NAV MARKERS Halo 4: StockFallback: %s (marker projector %s, camera build %s); markers keep the game's "
            "own placement. Retail-derived lead, not H4EK-verified",
            BindName(bind), projector ? "matched" : "unmatched", build ? "matched" : "unmatched");
        return false;
    }
    feature.base = base;
    feature.generation = generation;
    feature.builder = reinterpret_cast<Halo4ProjectionBuilderFn>(base + static_cast<uintptr_t>(sites.projectionRva));
    feature.proof = MarkerProof{};
    feature.proving.store(false, std::memory_order_relaxed);
    feature.proven.store(false, std::memory_order_release);
    feature.provenCount.store(0, std::memory_order_relaxed);
    feature.withdrawnCount.store(0, std::memory_order_relaxed);
    for (auto& count : feature.uses)
        count.store(0, std::memory_order_relaxed);
    for (auto& count : feature.steps)
        count.store(0, std::memory_order_relaxed);
    feature.reportedProven = feature.reportedWithdrawn = 0;
    feature.reportedFirstUse = false;
    void* target = reinterpret_cast<void*>(projector);
    const auto created =
        MH_CreateHook(target, reinterpret_cast<void*>(&Halo4MarkerProjectorDetour), &feature.original);
    if (created != MH_OK)
    {
        feature.original = nullptr;
        LOG("NAV MARKERS Halo 4: StockFallback: hook creation failed (%d)", static_cast<int>(created));
        return false;
    }
    feature.target = target;
    const auto enabled = MH_EnableHook(target);
    if (enabled != MH_OK)
    {
        LOG("NAV MARKERS Halo 4: StockFallback: hook enable failed (%d)", static_cast<int>(enabled));
        (void)RemoveHalo4NavMarkers();
        return false;
    }
    feature.enabled.store(true, std::memory_order_release);
    LOG("NAV MARKERS Halo 4: Installed (RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED): the HUD's marker projector "
        "halo4.dll+0x%llX is hooked; outside the eye passes it is handed the head's pose (and, with the field-of-view "
        "switch, the eyes' vertical field of view) in a copy of its camera, projected by the game's builder +0x%llX. "
        "UNPROVEN until the camera it is handed has agreed with the published aim on %u calls across %.0f degrees: "
        "until then nothing is changed; follow-head switch=%d, field-of-view switch=%d",
        static_cast<unsigned long long>(sites.projectorRva), static_cast<unsigned long long>(sites.projectionRva),
        kProofSamples, kProofSpreadRad * head_relative::kRadToDeg, g_config.nav_markers_follow_head ? 1 : 0,
        g_config.nav_markers_on_target ? 1 : 0);
    return true;
}

const char* Halo4NavUseName(size_t use)
{
    return halo4_nav_markers::UseName(static_cast<halo4_nav_markers::Use>(use));
}
// The worker's two-second report. Every build: the proof's changes, and one line the first time a marker is placed
// from the head. Dev builds: the window's verdicts and proof steps.
void ReportHalo4NavMarkers()
{
    using halo4_nav_markers::Use;
    auto& feature = g_halo4NavMarkers;
    if (!feature.target)
        return;
    const uint32_t proven = feature.provenCount.load(std::memory_order_relaxed);
    const uint32_t withdrawn = feature.withdrawnCount.load(std::memory_order_relaxed);
    if (proven != feature.reportedProven)
    {
        feature.reportedProven = proven;
        LOG("NAV MARKERS Halo 4: the HUD's marker camera is proven to be the aim camera (it agreed with the published "
            "aim on %u calls across at least %.0f degrees); markers are now placed from the head",
            halo4_nav_markers::kProofSamples, halo4_nav_markers::kProofSpreadRad * head_relative::kRadToDeg);
    }
    if (withdrawn != feature.reportedWithdrawn)
    {
        feature.reportedWithdrawn = withdrawn;
        LOG("NAV MARKERS Halo 4: the proof is WITHDRAWN (%u disagreements in a row with the published aim); markers "
            "keep the game's placement until it is proven again", halo4_nav_markers::kProofWithdrawRun);
    }
    uint32_t uses[kHalo4NavUseCount]{}, total = 0;
    for (size_t i = 0; i < kHalo4NavUseCount; ++i)
    {
        uses[i] = feature.uses[i].exchange(0, std::memory_order_relaxed);
        total += uses[i];
    }
    const uint32_t used = uses[static_cast<size_t>(Use::Used)];
    if (used && !feature.reportedFirstUse)
    {
        feature.reportedFirstUse = true;
        LOG("NAV MARKERS Halo 4: objective markers and their arrows are placed from the head, not the gun (the first "
            "one: the head faced %.1f deg away from the aim)", feature.lastTurnDeg.load(std::memory_order_relaxed));
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    uint32_t steps[kHalo4NavProofCount]{};
    for (size_t i = 0; i < kHalo4NavProofCount; ++i)
        steps[i] = feature.steps[i].exchange(0, std::memory_order_relaxed);
    if (total)
    {
        char reasons[320];
        const int length = ReachHeadRelativeReasons(reasons, sizeof(reasons), uses, kHalo4NavUseCount,
                                                    static_cast<size_t>(Use::Used), &Halo4NavUseName);
        using halo4_nav_markers::ProofStep;
        LOG("NAV MARKERS Halo 4 (dev): %u marker placements: %u given the head (last: %.1f deg from the aim); the "
            "others: %s; proof %s (agreed %u, lagging %u, disagreed %u, no reference %u); follow-head switch=%d",
            total, used, feature.lastTurnDeg.load(std::memory_order_relaxed), length ? reasons : "none",
            feature.proven.load(std::memory_order_relaxed) ? "PROVEN" : "unproven",
            steps[static_cast<size_t>(ProofStep::Agreed)] + steps[static_cast<size_t>(ProofStep::Proven)],
            steps[static_cast<size_t>(ProofStep::Lagging)], steps[static_cast<size_t>(ProofStep::Disagreed)],
            steps[static_cast<size_t>(ProofStep::NoReference)], g_config.nav_markers_follow_head ? 1 : 0);
    }
#else
    (void)total;
#endif
}
