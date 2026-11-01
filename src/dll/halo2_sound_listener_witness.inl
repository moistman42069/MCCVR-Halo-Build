// Included by halo2_observer_6dof.cpp inside its anonymous namespace, after halo2_head_relative.inl, and ONLY in
// dev builds (the include itself is inside #if HALOMCCVR_DEV_DIAGNOSTICS): no byte of this file is in a player
// build. Bound with the observer core (InstallCore), retired with it, reported from Halo2Observer6Dof_Poll.
//
// HALO 2: THE SOUND LISTENER WITNESS -- the runtime half. The sites, the record's decode and the reasoning are in
// src/common/halo2_sound_listener_logic.h; what one window means is src/common/sound_listener_witness_logic.h;
// the write-up is docs/HALO2-HEAD-RELATIVE-2026-10-07.md ("Sound").
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H2EK-VERIFIED. This file is how the lead gets measured.
//
// A MEASUREMENT, NOT A BEHAVIOUR. It installs no hook and writes nothing the game reads. It reads the game's own
// listener record for local user 0 and holds its rows against three things this mod already has:
//   * THE HEAD: the tracked camera the observer core last wrote into the observer result and published (E-H2-6);
//   * THE AIM: player control's desired angles for user 0, the record the direct drive writes the hand into
//     (E-H2-81), turned into a forward;
//   * THE GAME'S OWN CAMERA: the stock camera of that same publication, as the engine built it before the head
//     was written (its position without the view's lift);
// and against THE OBSERVER RESULT as it stands, the record the game builds the listener from.
//
// WHEN IT SAMPLES. The motion tracker's lane already hooks the desired-angles accessor, and the tracker's sampler
// calls it once a game tick for each playing user. That call is made inside the pass's game ticks: AFTER the
// previous pass's sound update built the listeners from the observer result the head was written into, and
// BEFORE this pass rebuilds either. At that moment the listener, the observer result and the newest publication
// all belong to the same pass, so a listener that follows the head measures 0.00, not "0 give or take a frame of
// head motion". The witness is one call in that detour (never a detour of its own). When no tick sampled for
// three seconds (a pause, a loading screen, or the tracker's hook did not install) the worker takes one sample
// itself and the line says so: that one can be a pass apart from the head it is held against.
//
// Cost per tick: one flag, two guarded copies of 0x48 bytes and of the observer's three spans, one seqlock copy of
// the publication, about forty float operations. No allocation, no logging, no waiting; the line is formatted on
// the worker.
struct Halo2ListenerWitness
{
    std::atomic<bool> bound{false};
    uintptr_t base = 0;
    uint32_t generation = 0;
    halo2_sound_listener::Sites sites;
    // One sampler at a time: tried, never waited for (the tracker's sampler is observed on one thread only, but
    // Halo 4's tracker build runs on job threads, so this does not rest on that).
    std::atomic<bool> sampling{false};
    listener_witness::Window open;        // under `sampling`
    uint32_t lastThread = 0;              // under `sampling`
    uint64_t nextSerial = 1;              // under `sampling`
    uint64_t lastTickMs = 0;              // under `sampling`: the last tick that sampled
    // The last finished window, for the worker (sequence lock: odd = being written).
    std::atomic<uint32_t> version{0};
    listener_witness::Window published;
    std::atomic<uint32_t> overlapped{0};
    // The worker only.
    uint64_t reportMs = 0, reportedSerial = 0, lastTickWindowMs = 0;
} g_halo2ListenerWitness;

void RetireHalo2ListenerWitness() noexcept
{
    g_halo2ListenerWitness.bound.store(false, std::memory_order_release);
}

// Guarded reads: the sound globals and the playing table are game-state allocations behind pointers the module
// holds, so nothing bounds them but the page they are on.
__declspec(noinline) bool Halo2ListenerWitnessRead(const Halo2ListenerWitness& witness, uintptr_t observer,
                                                    uint8_t* first, uint8_t* second, bool& playing, bool& globalsNull,
                                                    listener_witness::Pose& root, bool& rootOk, float& yaw,
                                                    float& pitch, bool& aimOk) noexcept
{
    const auto& sites = witness.sites;
    globalsNull = false;
    rootOk = false;
    aimOk = false;
    __try
    {
        const uintptr_t globals =
            *reinterpret_cast<const volatile uintptr_t*>(witness.base + static_cast<uintptr_t>(sites.globalsSlotRva));
        if (!globals)
        {
            globalsNull = true;
            return true;
        }
        const uintptr_t table =
            *reinterpret_cast<const volatile uintptr_t*>(witness.base + static_cast<uintptr_t>(sites.playingSlotRva));
        playing = table && *reinterpret_cast<const volatile int32_t*>(
                               table + sites.playingOffset +
                               kOwnedUser * halo2_sound_listener::kPlayingStride) != -1;
        const uint8_t* record =
            reinterpret_cast<const uint8_t*>(globals + sites.listenerOffset + kOwnedUser * sites.stride);
        std::memcpy(first, record, sites.stride);
        // The record's source as it stands, between the two reads of the record.
        if (observer)
        {
            listener_witness::Pose a, b;
            const uint8_t* result = reinterpret_cast<const uint8_t*>(observer);
            std::memcpy(a.position, result + sites.observerPosition, 12);
            std::memcpy(a.forward, result + sites.observerForward, 12);
            std::memcpy(a.up, result + sites.observerUp, 12);
            std::memcpy(b.position, result + sites.observerPosition, 12);
            std::memcpy(b.forward, result + sites.observerForward, 12);
            std::memcpy(b.up, result + sites.observerUp, 12);
            rootOk = std::memcmp(&a, &b, sizeof(a)) == 0 && listener_witness::Finite3(a.position) &&
                     listener_witness::Finite3(a.forward) && listener_witness::Finite3(a.up);
            root = a;
        }
        std::memcpy(second, record, sites.stride);
        // The aim: player control's desired angles, through the slot the direct drive bound on its own.
        const auto& drive = g_halo2Drive;
        if (drive.installed.load(std::memory_order_acquire) && drive.slot)
        {
            const uintptr_t control = *reinterpret_cast<const volatile uintptr_t*>(drive.slot);
            if (control)
            {
                const uintptr_t pc = control + kOwnedUser * kHalo2PcStride;
                yaw = *reinterpret_cast<const volatile float*>(pc + kHalo2PcDesiredYaw);
                pitch = *reinterpret_cast<const volatile float*>(pc + kHalo2PcDesiredPitch);
                aimOk = std::isfinite(yaw) && std::isfinite(pitch);
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// One sample into `window`. Any thread; reads only.
void Halo2ListenerWitnessTake(const Halo2ListenerWitness& witness, listener_witness::Window& window) noexcept
{
    using namespace listener_witness;
    const auto refuse = [&window](Refusal why) { ++window.refused[static_cast<size_t>(why)]; };
    // Only for the module instance it was decoded from, armed in a live level: RemoveCore clears g_armed and
    // raises the teardown flag before the module can go.
    const uint32_t generation = g_generation.load(std::memory_order_acquire);
    if (!g_armed.load(std::memory_order_acquire) || !g_levelLive.load(std::memory_order_acquire) ||
        g_teardownRequested.load(std::memory_order_acquire) ||
        g_moduleBase.load(std::memory_order_acquire) != witness.base || generation != witness.generation)
    {
        refuse(Refusal::NotArmed);
        return;
    }
    const uintptr_t observerArray = g_observerResult.load(std::memory_order_acquire);
    const uintptr_t observer = observerArray ? observerArray + Halo2ObserverRecordOffset(kOwnedUser) : 0;
    uint8_t first[0x80]{}, second[0x80]{};
    bool playing = false, globalsNull = false, rootOk = false, aimOk = false;
    float yaw = 0.0f, pitch = 0.0f;
    Pose root;
    if (!Halo2ListenerWitnessRead(witness, observer, first, second, playing, globalsNull, root, rootOk, yaw, pitch,
                                  aimOk))
    {
        refuse(Refusal::Unreadable);
        return;
    }
    if (globalsNull)
    {
        refuse(Refusal::NoGlobals);
        return;
    }
    halo2_sound_listener::Record record;
    const halo2_sound_listener::Read read =
        halo2_sound_listener::ReadListener(first, second, witness.sites, playing, record);
    if (read != halo2_sound_listener::Read::Ok)
    {
        refuse(halo2_sound_listener::RefusalOf(read));
        return;
    }
    // The head and the game's own camera: one publication, checked for this load and for age like every other
    // reader of it (100 ms on the sight clock).
    Halo2ObserverPosePublication publication{};
    head_relative::HeadSample sample;
    if (Halo2Observer6Dof_ReadPublishedPose(publication))
    {
        sample.stampUs = publication.publishedUs;
        sample.generation = publication.generation;
        sample.valid = publication.snapshot.valid;
        for (int axis = 0; axis < 3; ++axis)
        {
            sample.forward[axis] = publication.tracked.forward[axis];
            sample.up[axis] = publication.tracked.up[axis];
        }
    }
    const head_relative::Use use = head_relative::SampleUse(sample, generation, Sight_NowUs());
    ++window.head[static_cast<size_t>(use)];
    Pose head, camera;
    References refs;
    if (use == head_relative::Use::Ok)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            head.position[axis] = publication.tracked.position[axis];
            head.forward[axis] = publication.tracked.forward[axis];
            head.up[axis] = publication.tracked.up[axis];
            camera.forward[axis] = publication.stock.forward[axis];
            camera.up[axis] = publication.stock.up[axis];
        }
        Halo2EngineEyePosition(publication, camera.position);
        refs.head = &head;
        refs.camera = &camera;
    }
    float aim[3]{};
    if (aimOk && ForwardOfAngles(yaw, pitch, aim))
        refs.aim = aim;
    if (rootOk)
        refs.root = &root;
    else
        ++window.rootMissing;
    AddSample(window, record.pose, refs);
    // The player's state in this sample, each from the reader the mod already has for it.
    if (Halo2Observer6Dof_VehicleControlActive())
        ++window.seated;
    switch (ReadHalo2DirectorPerspective())
    {
    case director_camera::Signal::Yes: ++window.firstPerson; break;
    case director_camera::Signal::No: break;
    default: ++window.perspectiveUnknown; break;
    }
    if (halo2_head_relative::ControlFresh(g_halo2Drive.controlMs.load(std::memory_order_acquire), GetTickCount64()))
        ++window.controlLive;
}

// The tracker's accessor detour, for the sampler's own call (once a game tick per playing user). No allocation,
// no logging, no waiting.
void Halo2ListenerWitnessTick(int32_t user) noexcept
{
    auto& witness = g_halo2ListenerWitness;
    if (user != static_cast<int32_t>(kOwnedUser) || !witness.bound.load(std::memory_order_acquire))
        return;
    if (witness.sampling.exchange(true, std::memory_order_acquire))
    {
        witness.overlapped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint64_t now = GetTickCount64();
    auto& open = witness.open;
    const auto publish = [&witness, &open](uint64_t closedMs) {
        open.closedMs = closedMs;
        open.serial = witness.nextSerial++;
        witness.version.fetch_add(1, std::memory_order_acq_rel);   // odd: being written
        witness.published = open;
        witness.version.fetch_add(1, std::memory_order_release);   // even: readable
        open = listener_witness::Window{};
    };
    // The ticks stopped for a second (a pause, a loading screen) with a window open: it ends where they stopped,
    // so no window mixes what was sampled before the gap with what comes after it.
    if (open.openedMs && witness.lastTickMs && now - witness.lastTickMs > 1000)
        publish(witness.lastTickMs);
    witness.lastTickMs = now;
    if (!open.openedMs)
        open.openedMs = now;
    const uint32_t thread = GetCurrentThreadId();
    if (witness.lastThread && witness.lastThread != thread)
        ++open.threadChanges;
    witness.lastThread = thread;
    open.thread = thread;
    Halo2ListenerWitnessTake(witness, open);
    if (now - open.openedMs >= listener_witness::kWindowMs)
        publish(now);
    witness.sampling.store(false, std::memory_order_release);
}

// Management thread, with the observer's install, after the tracker's (it holds nothing of the tracker's but
// the helpers that read the image as it stood before any hook of ours). Reads only; no hook.
bool BindHalo2ListenerWitness(uintptr_t base, size_t size, uint32_t generation, uintptr_t observerResultArray)
{
    using namespace halo2_sound_listener;
    auto& witness = g_halo2ListenerWitness;
    RetireHalo2ListenerWitness();
    if (!base || !generation || size < 0x1000 || observerResultArray < base || observerResultArray - base >= size)
    {
        LOG("SOUND LISTENER Halo 2 (dev witness): OFF: no title image, generation or observer result array");
        return false;
    }
    // No tick can be inside: the tracker's hook, the only caller, was drained before this install could begin.
    if (witness.sampling.exchange(true, std::memory_order_acquire))
    {
        LOG("SOUND LISTENER Halo 2 (dev witness): OFF: a sample from the previous load is still in progress");
        return false;
    }
    const uintptr_t loop = Halo2HeadRelativeUnique(base, size, kListenerLoopPattern);
    const uintptr_t build = Halo2HeadRelativeUnique(base, size, kListenerBuildPattern);
    const uintptr_t builder = Halo2HeadRelativeUnique(base, size, kListenerMatrixBuilderPattern);
    const uintptr_t accessor = Halo2HeadRelativeUnique(base, size, kHalo2ObserverResultAccessorPattern);
    uint8_t loopBytes[kLoopBytes]{}, buildBytes[kBuildBytes]{}, builderBytes[kBuilderBytes]{},
        playingBytes[kPlayingBytes]{}, accessorBytes[kHalo2ObserverResultAccessorNextOffset]{};
    const bool loopRead = Halo2HeadRelativeRead(base, size, loop, loopBytes, kLoopBytes);
    const bool buildRead = Halo2HeadRelativeRead(base, size, build, buildBytes, kBuildBytes);
    const bool builderRead = Halo2HeadRelativeRead(base, size, builder, builderBytes, kBuilderBytes);
    const bool accessorRead = Halo2HeadRelativeRead(base, size, accessor, accessorBytes, sizeof(accessorBytes));
    bool playingRead = false;
    if (loopRead)
    {
        const int64_t playingRva = BranchRva(loopBytes, kLoopCallPlaying, static_cast<int64_t>(loop - base));
        playingRead = playingRva > 0 && Halo2HeadRelativeRead(base, size, base + static_cast<uintptr_t>(playingRva),
                                                             playingBytes, kPlayingBytes);
    }
    ObserverRecord observer;
    observer.accessorMatchRva = accessor ? static_cast<int64_t>(accessor - base) : -1;
    observer.arrayRva = static_cast<int64_t>(observerResultArray - base);
    observer.stride = kHalo2ObserverStride;
    observer.position = kHalo2ObserverResultPositionOffset;
    observer.forward = kHalo2ObserverResultForwardOffset;
    observer.up = kHalo2ObserverResultUpOffset;
    observer.strideAt = kHalo2ObserverResultAccessorStrideOffset;
    observer.dispAt = kHalo2ObserverResultAccessorDispOffset;
    observer.nextAt = kHalo2ObserverResultAccessorNextOffset;
    Sites sites;
    Bind bind = DecodeSites(loopRead ? loopBytes : nullptr, static_cast<int64_t>(loop - base),
                            buildRead ? buildBytes : nullptr, static_cast<int64_t>(build - base),
                            builderRead ? builderBytes : nullptr, static_cast<int64_t>(builder - base),
                            playingRead ? playingBytes : nullptr, accessorRead ? accessorBytes : nullptr, observer,
                            sites);
    // Every slot the witness will read through lies inside the module, and one listener fits the sample's buffer.
    if (bind == Bind::Bound &&
        (static_cast<uint64_t>(sites.globalsSlotRva) + sizeof(uintptr_t) > size ||
         static_cast<uint64_t>(sites.playingSlotRva) + sizeof(uintptr_t) > size || sites.stride > 0x80 ||
         static_cast<uint64_t>(sites.observerArrayRva) + 4ull * sites.observerStride > size))
        bind = Bind::Implausible;
    if (bind != Bind::Bound)
    {
        witness.sampling.store(false, std::memory_order_release);
        LOG("SOUND LISTENER Halo 2 (dev witness): OFF: %s (loop head %s, build tail %s, matrix builder %s, observer "
            "accessor %s); nothing is measured and nothing is changed. Retail-derived lead, not H2EK-verified",
            BindName(bind), loop ? "matched" : "unmatched", build ? "matched" : "unmatched",
            builder ? "matched" : "unmatched", accessor ? "matched" : "unmatched");
        return false;
    }
    witness.base = base;
    witness.generation = generation;
    witness.sites = sites;
    witness.open = listener_witness::Window{};
    witness.lastThread = 0;
    witness.nextSerial = 1;
    witness.lastTickMs = 0;
    witness.version.fetch_add(1, std::memory_order_acq_rel);
    witness.published = listener_witness::Window{};
    witness.version.fetch_add(1, std::memory_order_release);
    witness.overlapped.store(0, std::memory_order_relaxed);
    witness.reportMs = GetTickCount64();
    witness.reportedSerial = 0;
    witness.lastTickWindowMs = witness.reportMs;
    witness.sampling.store(false, std::memory_order_release);
    witness.bound.store(true, std::memory_order_release);
    LOG("SOUND LISTENER Halo 2 (dev witness): bound (RETAIL-DERIVED LEAD, NOT H2EK-VERIFIED; reads only, no hook, "
        "nothing the game reads is written). The listener update's loop halo2.dll+0x%llX fills listener 0 at "
        "*[+0x%llX] + 0x%X (stride 0x%X, %u listeners; valid byte +0x%X, matrix +0x%X, velocity +0x%X) for a user "
        "the game's own test +0x%llX says is playing (*[+0x%llX] + 0x%X + user * %u is not -1), from "
        "+0x%llX(user) = the observer result +0x%llX + user * 0x%X: the array this mod's observer core writes the "
        "head into. MATRIX CONVENTION, from the builder +0x%llX's own stores: scale +0x%X = 1.0; forward +0x%X <- "
        "the observer result's +0x%X; left +0x%X = up x forward; up +0x%X <- its +0x%X; position +0x%X <- its "
        "+0x%X (0x%X bytes). Every ~2 s one line holds listener 0 against the head, the aim, the game's own camera "
        "and the observer result",
        static_cast<unsigned long long>(sites.loopRva), static_cast<unsigned long long>(sites.globalsSlotRva),
        sites.listenerOffset, sites.stride, sites.users, sites.validOffset, sites.matrixOffset, sites.velocityOffset,
        static_cast<unsigned long long>(sites.playingRva), static_cast<unsigned long long>(sites.playingSlotRva),
        sites.playingOffset, kPlayingStride, static_cast<unsigned long long>(sites.accessorRva),
        static_cast<unsigned long long>(sites.observerArrayRva), sites.observerStride,
        static_cast<unsigned long long>(sites.builderRva), sites.scaleOffset, sites.forwardOffset,
        sites.observerForward, sites.leftOffset, sites.upOffset, sites.observerUp, sites.positionOffset,
        sites.observerPosition, sites.matrixBytes);
    return true;
}

// The title worker, every 2 s (its own clock). Formats and logs; the sampler never does.
void ReportHalo2ListenerWitness() noexcept
{
    using namespace listener_witness;
    auto& witness = g_halo2ListenerWitness;
    if (!witness.bound.load(std::memory_order_acquire))
        return;
    const uint64_t now = GetTickCount64();
    if (now - witness.reportMs < kWindowMs)
        return;
    witness.reportMs = now;
    Window window;
    bool have = false;
    for (int attempt = 0; attempt < 4 && !have; ++attempt)
    {
        const uint32_t before = witness.version.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        window = witness.published;
        have = witness.version.load(std::memory_order_acquire) == before;
    }
    const char* source = "at the game tick (the motion sensor's sampler, same pass as the head)";
    if (have && window.serial && window.serial != witness.reportedSerial)
    {
        witness.reportedSerial = window.serial;
        witness.lastTickWindowMs = now;
    }
    else
    {
        // No tick window closed since the last report. That is ordinary once in a while (the two clocks are not
        // in step); after three seconds it is a state with no game ticks, and the worker looks for itself.
        if (now - witness.lastTickWindowMs < 3000)
            return;
        window = Window{};
        window.openedMs = window.closedMs = now;
        window.thread = GetCurrentThreadId();
        Halo2ListenerWitnessTake(witness, window);
        source = "BY THE WORKER, one sample (no game tick sampled for 3 s: a pause, a loading screen, or the "
                 "tracker's accessor hook is not installed; this sample can be a pass apart from the head)";
    }
    const uint32_t overlapped = witness.overlapped.exchange(0, std::memory_order_relaxed);
    window.refused[static_cast<size_t>(Refusal::Overlapped)] += overlapped;
    char refusals[512], headMissing[256];
    RefusalText(window, refusals, sizeof(refusals));
    HeadMissingText(window, headMissing, sizeof(headMissing));
    const auto& sites = witness.sites;
    const Measure& hf = window[M::HeadForward];
    const Measure& hu = window[M::HeadUp];
    const Measure& hd = window[M::HeadDistance];
    const Measure& af = window[M::AimForward];
    const Measure& cf = window[M::CameraForward];
    const Measure& cu = window[M::CameraUp];
    const Measure& cd = window[M::CameraDistance];
    const Measure& rf = window[M::RootForward];
    const Measure& ru = window[M::RootUp];
    const Measure& rd = window[M::RootDistance];
    const Measure& apart = window[M::HeadAimApart];
    LOG("SOUND LISTENER Halo 2 (dev witness): %s, %s | listener 0 vs HEAD (the camera this mod wrote into the "
        "observer result): forward mean %.2f max %.2f deg, up mean %.2f max %.2f deg, position mean %.4f max %.4f "
        "wu (n=%u) | vs AIM (player control's desired angles, where the direct drive writes the hand): forward "
        "mean %.2f max %.2f deg (n=%u) | vs THE GAME'S OWN CAMERA (before the head was written): forward mean "
        "%.2f max %.2f deg, up mean %.2f max %.2f deg, position mean %.4f max %.4f wu (n=%u) | vs THE OBSERVER "
        "RESULT as it stood: forward mean %.2f max %.2f deg, up mean %.2f max %.2f deg, position mean %.4f max "
        "%.4f wu (n=%u) | head and aim apart: mean %.2f max %.2f deg (n=%u) | samples: %u taken, %u refused "
        "(%s); of the taken, no usable head on %u (%s), no aim on %u, no observer result on %u; seated %u, first "
        "person by the director %u (unread %u), control update running %u | sampled %s on thread %u (%u thread "
        "changes), window %llu ms | matrix: scale +0x%X, forward +0x%X, left +0x%X = up x forward, up +0x%X, "
        "position +0x%X (decoded from the builder's stores); each sample's rows were a rotation of scale 1",
        VerdictName(Orientation(window)), PlaceName(Position(window)), hf.Mean(), hf.max, hu.Mean(), hu.max,
        hd.Mean(), hd.max, hf.count, af.Mean(), af.max, af.count, cf.Mean(), cf.max, cu.Mean(), cu.max, cd.Mean(),
        cd.max, cf.count, rf.Mean(), rf.max, ru.Mean(), ru.max, rd.Mean(), rd.max, rf.count, apart.Mean(),
        apart.max, apart.count, window.taken, window.Refused(), refusals, window.HeadMissing(), headMissing,
        window.aimMissing, window.rootMissing, window.seated, window.firstPerson, window.perspectiveUnknown,
        window.controlLive, source, window.thread, window.threadChanges,
        static_cast<unsigned long long>(window.closedMs - window.openedMs), sites.scaleOffset, sites.forwardOffset,
        sites.leftOffset, sites.upOffset, sites.positionOffset);
}
