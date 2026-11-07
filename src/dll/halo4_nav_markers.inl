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
// +0x40B180: one point onto the picture (point, camera, projection, projection again, on-screen out, u/v out).
// tools/audit_caller_area.py 40B180:10:i is clean: it reads its caller's area to entry+0x30 (the sixth argument),
// and neither native caller keeps a volatile register across it.
using Halo4MarkerPointFn = uint64_t (__fastcall*)(void* point, void* camera, void* projection, void* again,
                                                  uint8_t* onScreen, float* uv);

constexpr size_t kHalo4NavUseCount = static_cast<size_t>(halo4_nav_markers::Use::Count);
constexpr size_t kHalo4NavProofCount = static_cast<size_t>(halo4_nav_markers::ProofStep::Count);
struct Halo4NavMarkersRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    void* target = nullptr;
    void* original = nullptr;                         // Halo4MarkerProjectorFn
    void* pointTarget = nullptr;
    void* pointOriginal = nullptr;                    // Halo4MarkerPointFn
    Halo4ProjectionBuilderFn builder = nullptr;
    // The HUD's affine undone (the point hook): points laid out where the affine shows them on target, and of
    // those, points that fall outside the HUD's own area and became its edge arrows. The last affine used.
    std::atomic<uint32_t> affineUndone{0}, affineOffArea{0};
    std::atomic<float> lastAffineScale{1.0f}, lastAffineHeight{0.0f};
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

// What the projector's detour hands its own call of the point projection: whether to undo the HUD's affine, and
// the affine. One thread, one call: plain data.
struct Halo4NavAffineCall
{
    bool active = false;
    halo4_nav_markers::HudAffine affine;
};
thread_local Halo4NavAffineCall g_halo4NavAffineCall;

// The HUD's affine as Halo4ApplyNativeHudAffine applies it now, against the height of the picture `camera` covers
// (its window, +0x30: y0, x0, y1, x1 in the render's pixels). False = the affine is not applied (no native HUD
// hook, or an aim frame pass, which lays its HUD out the flat game's way) or not usable.
bool Halo4NavHudAffine(const uint8_t* camera, halo4_nav_markers::HudAffine& out) noexcept
{
    if (!g_halo4Restoration.hudInstalled.load(std::memory_order_acquire) ||
        g_aimFramePass.load(std::memory_order_acquire))
        return false;
    Halo4HudAffine affine{};
    if (!Halo4ComputeNativeHudAffine(g_config.hud_size, g_config.hud_aspect, g_config.hud_vertical_offset, affine))
        return false;
    int16_t window[4]{};
    memcpy(window, camera + 0x30, sizeof(window));
    return halo4_nav_markers::MakeHudAffine(affine.horizontal, affine.vertical, affine.heightPixels,
                                            static_cast<float>(window[2] - window[0]), out);
}

// One call, outside the detour's own frame so a fault in it is caught here. Fills `camera` and `projection` (the
// copies handed to the game) and `affine` only for Used.
__declspec(noinline) halo4_nav_markers::Use Halo4NavMarkerHead(const void* gameCamera, const void* gameProjection,
                                                               uint8_t* camera, uint8_t* projection,
                                                               Halo4NavAffineCall& affine) noexcept
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
            affine.active = Halo4NavHudAffine(camera, affine.affine);
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
        Halo4NavAffineCall affine;
        if (camera && projection && feature.enabled.load(std::memory_order_acquire) &&
            Halo4NavMarkerHead(camera, projection, headCamera, headProjection, affine) ==
                halo4_nav_markers::Use::Used)
        {
            useCamera = headCamera;
            useProjection = headProjection;
        }
        // The point projection inside this call undoes the HUD's affine (Halo4MarkerPointDetour).
        g_halo4NavAffineCall = affine;
        __try
        {
            return CallNativeWithSpareSlots(original, point, clamp, useCamera, useProjection, a5, a6, a7, a8, a9,
                                            a10, a11, a12);
        }
        __finally
        {
            g_halo4NavAffineCall.active = false;
        }
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_release);
    }
}

// The point projection. Forwarded untouched unless the projector's detour, on this thread and in this call, asked
// for the HUD's affine to be undone (only for a marker placed from the head).
__declspec(noinline) uint64_t __fastcall Halo4MarkerPointDetour(void* point, void* camera, void* projection,
                                                                void* again, uint8_t* onScreen, float* uv)
{
    auto& feature = g_halo4NavMarkers;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        const auto original = reinterpret_cast<Halo4MarkerPointFn>(feature.pointOriginal);
        if (!original)
            return 0;
        const uint64_t result = CallNativeWithSpareSlots(original, point, camera, projection, again, onScreen, uv);
        const Halo4NavAffineCall& call = g_halo4NavAffineCall;
        if (call.active && onScreen && uv)
        {
            __try
            {
                bool on = *onScreen != 0;
                if (halo4_nav_markers::UndoHudAffine(call.affine, on, uv))
                {
                    feature.affineUndone.fetch_add(1, std::memory_order_relaxed);
                    if (!on)
                    {
                        *onScreen = 0;
                        feature.affineOffArea.fetch_add(1, std::memory_order_relaxed);
                    }
                    feature.lastAffineScale.store(call.affine.vertical, std::memory_order_relaxed);
                    feature.lastAffineHeight.store(call.affine.heightNdc, std::memory_order_relaxed);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                feature.uses[static_cast<size_t>(halo4_nav_markers::Use::Fault)].fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
        return result;
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
    if (!feature.target && !feature.pointTarget)
        return true;
    for (void* target : {feature.target, feature.pointTarget})
    {
        if (!target)
            continue;
        const auto disabled = MCCVR_DisableHookForRetirement(target);
        if (disabled != MH_OK && disabled != MH_ERROR_DISABLED && disabled != MH_ERROR_NOT_CREATED)
        {
            LOG("NAV MARKERS Halo 4: CleanupRequired: disable failed (%d)", static_cast<int>(disabled));
            return false;
        }
    }
    const void* functions[]{reinterpret_cast<const void*>(&Halo4MarkerProjectorDetour),
                            reinterpret_cast<const void*>(&Halo4MarkerPointDetour)};
    const void* trampolines[]{feature.original, feature.pointOriginal};
    if (!WaitForNativeDetourQuiescence(functions, trampolines, 2, feature.callbacks))
    {
        LOG("NAV MARKERS Halo 4: CleanupRequired: the detour is busy");
        return false;
    }
    for (void** target : {&feature.target, &feature.pointTarget})
    {
        if (!*target)
            continue;
        const auto removed = MH_RemoveHook(*target);
        if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
        {
            LOG("NAV MARKERS Halo 4: CleanupRequired: removal failed (%d)", static_cast<int>(removed));
            return false;
        }
        *target = nullptr;
    }
    feature.original = nullptr;
    feature.pointOriginal = nullptr;
    feature.builder = nullptr;
    return true;
}
// Cold, on the title's install thread, after the camera core. Binds nothing unless both patterns hold on this image;
// a refusal is the game's own marker placement, said once.
bool InstallHalo4NavMarkers(uintptr_t base, size_t size, uint32_t generation)
{
    using namespace halo4_nav_markers;
    auto& feature = g_halo4NavMarkers;
    if (feature.target || feature.pointTarget)
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
    feature.affineUndone.store(0, std::memory_order_relaxed);
    feature.affineOffArea.store(0, std::memory_order_relaxed);
    void* target = reinterpret_cast<void*>(projector);
    void* pointTarget = reinterpret_cast<void*>(base + static_cast<uintptr_t>(sites.pointRva));
    const auto created =
        MH_CreateHook(target, reinterpret_cast<void*>(&Halo4MarkerProjectorDetour), &feature.original);
    if (created != MH_OK)
    {
        feature.original = nullptr;
        LOG("NAV MARKERS Halo 4: StockFallback: hook creation failed (%d)", static_cast<int>(created));
        return false;
    }
    feature.target = target;
    const auto pointCreated =
        MH_CreateHook(pointTarget, reinterpret_cast<void*>(&Halo4MarkerPointDetour), &feature.pointOriginal);
    if (pointCreated != MH_OK)
    {
        feature.pointOriginal = nullptr;
        LOG("NAV MARKERS Halo 4: StockFallback: the point projection's hook creation failed (%d)",
            static_cast<int>(pointCreated));
        (void)RemoveHalo4NavMarkers();
        return false;
    }
    feature.pointTarget = pointTarget;
    const auto pointEnabled = MH_EnableHook(pointTarget);
    const auto enabled = pointEnabled == MH_OK ? MH_EnableHook(target) : pointEnabled;
    if (enabled != MH_OK)
    {
        LOG("NAV MARKERS Halo 4: StockFallback: hook enable failed (%d)", static_cast<int>(enabled));
        (void)RemoveHalo4NavMarkers();
        return false;
    }
    feature.enabled.store(true, std::memory_order_release);
    LOG("NAV MARKERS Halo 4: Installed (RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED): the HUD's marker projector "
        "halo4.dll+0x%llX is hooked; outside the eye passes it is handed the head's pose (and, with the field-of-view "
        "switch, the eyes' vertical field of view) in a copy of its camera, projected by the game's builder +0x%llX; "
        "its point projection +0x%llX then undoes the HUD's size, aspect and height for those markers. UNPROVEN until the camera it is handed has agreed with the published aim on %u calls across %.0f degrees: "
        "until then nothing is changed; follow-head switch=%d, field-of-view switch=%d",
        static_cast<unsigned long long>(sites.projectorRva), static_cast<unsigned long long>(sites.projectionRva),
        static_cast<unsigned long long>(sites.pointRva), kProofSamples, kProofSpreadRad * head_relative::kRadToDeg, g_config.nav_markers_follow_head ? 1 : 0,
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
    const uint32_t undone = feature.affineUndone.exchange(0, std::memory_order_relaxed);
    const uint32_t offArea = feature.affineOffArea.exchange(0, std::memory_order_relaxed);
    if (used && !feature.reportedFirstUse)
    {
        feature.reportedFirstUse = true;
        LOG("NAV MARKERS Halo 4: objective markers and their arrows are placed from the head, not the gun (the first "
            "one: the head faced %.1f deg away from the aim); the HUD's size and height are undone for them: %s "
            "(vertical scale %.2f, height %.3f of the half picture)",
            feature.lastTurnDeg.load(std::memory_order_relaxed), undone ? "yes" : "not yet",
            feature.lastAffineScale.load(std::memory_order_relaxed),
            feature.lastAffineHeight.load(std::memory_order_relaxed));
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
            "others: %s; proof %s (agreed %u, lagging %u, disagreed %u, no reference %u); the HUD's affine undone "
            "for %u points (%u of them outside the HUD's area: its edge arrows); follow-head switch=%d",
            total, used, feature.lastTurnDeg.load(std::memory_order_relaxed), length ? reasons : "none",
            feature.proven.load(std::memory_order_relaxed) ? "PROVEN" : "unproven",
            steps[static_cast<size_t>(ProofStep::Agreed)] + steps[static_cast<size_t>(ProofStep::Proven)],
            steps[static_cast<size_t>(ProofStep::Lagging)], steps[static_cast<size_t>(ProofStep::Disagreed)],
            steps[static_cast<size_t>(ProofStep::NoReference)], undone, offArea,
            g_config.nav_markers_follow_head ? 1 : 0);
    }
#else
    (void)total;
    (void)offArea;
#endif
}
