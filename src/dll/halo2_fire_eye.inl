// FLAT PARITY, Halo 2 on foot (sight_layer.h SightFireEyeFn; shot_lag_logic.h 4e). A muzzle shot flies
// through P' = P + (E_fire - E_P): E_P the engine eye of the observer publication P was built from, E_fire the eye
// the round leaves from in flat -- the eye AFTER this tick's move, one tick of travel (or more) ahead of the newest
// render. Its source here is the firing assist's own camera evaluator, 0x6D4730 (H2EK 489E10, "with native
// perspective return"; DUAL-NATIVE-TARGETING-2026-09-18.md), which the assist calls at 0x7597B8 inside the fire
// 0x8E4940 (Halo2IndependentCameraDetour below). Asked for our unit inside the same fire, it answers the eye the
// assist traces from.
//
// Its answer counts as E_fire only once PROVEN (shot_lag::EvaluatorEyeProof): at rest it must agree with the
// publication's engine eye within 5 cm for a streak of samples, and only on the perspective it answered while
// agreeing. Samples come from every firing-assist call for our unit (the game's own call: no extra native work), from
// the muzzle shot's own ask, and from the unit's own tick while it stands still
// (Halo2NoteFireEyeTick). Until then -- and on any other perspective -- E_fire is E_P carried one measured tick
// (shot_lag::ChooseSampleFireEye), the flat probe's model. Every verdict is counted on the dev SHOT LAG transport
// line. Fire thread (the simulation thread) only.
//
// WHY THE ONE-TICK CARRY IS SHORT IN HALO 2 (static analysis of the pinned halo2.dll; the flat probe and
// the AS SEEN witness measure it). The unit camera position 0x927170 -- the evaluator's first-person path,
// 0x6D4730 -> 0x77C700 -> 0x8F9190 -> 0x927170 -- RECORDS the tick's camera into MCC's frame interpolator while a tick
// runs (0x7231C0; flag 0x164B304, set by the tick begin 0x723520) and, outside a tick, READS BACK
// lerp(previous tick, newest tick, factor 0x164B300) (0x7227A0). The factor is clamp(game_time+0x10, 0, 1), written
// after the frame's ticks (0x67A220+0x618) just before observer_update_all (+0x628). So the observer -- and the
// publication E_P comes from -- holds the INTERPOLATED eye, (1 - factor) of a tick behind the newest tick's, while the
// shot fires one tick later still: E_fire - E_P = (2 - factor) ticks of travel, not one. Asked inside the fire (a
// tick), the evaluator answers that tick's raw eye -- the post-move eye when the weapon fires after its unit moved,
// which the dev line below measures rather than assumes -- so a MEASURED E_fire carries exactly what flat fires from;
// the carried fallback is short by (1 - factor) of a tick (~half a tick on average: ~50 mm at 6 m/s). The game's own
// per-tick record of that camera (players update 0x6A2D80 -> 0x6D4730, after objects update) comes later in the same
// tick than any call of ours, so asking the evaluator never changes what the renderer interpolates.
//
// Samples from fires alone can leave the proof unlatched for a whole game (no PROVEN line; "fire eye: measured 0" on
// every transport line): a player who strafes whenever he fires never gives it a sample at rest. The tick
// feed asks the evaluator while our unit stands still, so the proof can latch before the first strafing shot; the dev
// HALO 2 FIRE EYE line counts every sample and every reason one did not count, and measures the evaluator's answer
// and the carried eye against E_P on every muzzle shot (proven or not), with the interpolator factor's prediction.
void* Halo2IndependentTargetStorage(uint32_t unit);

#if HALOMCCVR_DEV_DIAGNOSTICS
// Why a proof sample did or did not count (dev, per report window; under the state lock).
struct Halo2FireEyeDiag
{
    enum Source : int { kAssist = 0, kShotAsk, kTick, kSources };
    uint32_t samples[kSources]{};
    uint32_t noGeneration = 0, noPublication = 0, otherGeneration = 0, vehicle = 0, stale = 0;
    uint32_t notOwned = 0, aimNotActive = 0, notArmed = 0, noStorage = 0;
    uint32_t moving = 0, velocityUnknown = 0;
    uint32_t restN = 0, restAgree = 0, restNeither = 0, restDisagree = 0, restNonFinite = 0, otherPath = 0;
    double restSumMm = 0.0, restMinMm = 0.0, restMaxMm = 0.0;
    uint32_t bestStreak = 0;
    int32_t paths[4]{};
    uint32_t pathCount[4]{};
    // The fire (every muzzle shot whose evaluator answered): its answer and the carried eye against E_P along the
    // eye's travel, the factor E_P was interpolated at, and the game's own tick length.
    uint32_t fires = 0, firesMoving = 0;
    double evalAlongMm = 0.0, carriedAlongMm = 0.0, evalMs = 0.0, carriedMs = 0.0, predictedMs = 0.0, alphaSum = 0.0;
    uint32_t alphaN = 0;
    double tickMsSum = 0.0;
    uint32_t tickMsN = 0;
};
#endif

struct Halo2FireEyeState
{
    SRWLOCK lock = SRWLOCK_INIT;
    shot_lag::EvaluatorEyeProof proof{};
    // What the log last said (a transition is logged once, every build: rare and load-bearing).
    shot_lag::EyeProof::State reportedState = shot_lag::EyeProof::State::Unproven;
    bool reportedPath = false;
#if HALOMCCVR_DEV_DIAGNOSTICS
    Halo2FireEyeDiag diag{};
#endif
} g_halo2FireEye;

// Identity: our owned on-foot biped (unseated, no controlling parent: the same unit the muzzle shot admits) while
// the controller owns Halo 2's aim. `reason` (dev counters): 1 not ours, 2 aim not the controller's, 3 not armed,
// 4 no target storage (seated, a controlling parent, unreadable).
bool Halo2FireEyeEligible(uint32_t unit, int* reason = nullptr)
{
    int why = 0;
    if (unit == UINT32_MAX || unit != Halo2OwnedUnit())
        why = 1;
    else if (!Game_Halo2ControllerAimActive())
        why = 2;
    else if (!Halo2Observer6Dof_DirectWeaponAimArmed())
        why = 3;
    else
    {
        bool storage = false;
        __try
        {
            storage = Halo2IndependentTargetStorage(unit) != nullptr;   // reads the unit: guarded
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            storage = false;
        }
        if (!storage)
            why = 4;
    }
    if (reason)
        *reason = why;
    return why == 0;
}

// The newest publication as a proof's E_P: read, this generation, on foot, fresh (100 ms). `reason` (dev): 1 no
// generation, 2 none published, 3 another generation, 4 a seat's, 5 stale or from the future.
bool Halo2FireEyePublication(Halo2ObserverPosePublication& publication, uint32_t generation, uint64_t nowUs,
                             int& reason)
{
    reason = 0;
    if (!generation)
        reason = 1;
    else if (!Halo2Observer6Dof_ReadPublishedPose(publication))
        reason = 2;
    else if (publication.generation != generation)
        reason = 3;
    else if (publication.vehicleReferenceValid)
        reason = 4;
    else if (!publication.publishedUs || nowUs < publication.publishedUs || nowUs - publication.publishedUs > 100000)
        reason = 5;
    return reason == 0;
}

// One sample for the proof: the evaluator's answer `position` (null: non-finite) on perspective `path`, for `unit`,
// against the newest publication's engine eye. Only our unit's samples reach it (another unit's call would end our
// stint), so the callers check the unit first. `source` (dev): Halo2FireEyeDiag::Source.
void Halo2NoteFireEyeProof(uint32_t unit, const float* position, int32_t path, int source = 0)
{
    Halo2ObserverPosePublication publication{};
    const uint32_t generation = g_generation.load(std::memory_order_acquire);
    const uint64_t nowUs = Sight_NowUs();
    int notRead = 0;
    const bool read = Halo2FireEyePublication(publication, generation, nowUs, notRead);
    float eye[3]{};
    Halo2EngineEyePosition(publication, eye);
    const bool atRest = read && shot_lag::EyeAtRest(publication.engineEyeVelValid, publication.engineEyeVel);
    float offsetMm = std::numeric_limits<float>::quiet_NaN();
    if (position && shot_lag::Finite3(position) && shot_lag::Finite3(eye))
    {
        const float d[3]{position[0] - eye[0], position[1] - eye[1], position[2] - eye[2]};
        offsetMm = shot_lag::Length(d) * shot_lag::kMmPerWu;
    }
    int ineligible = 0;
    const bool eligible = read && Halo2FireEyeEligible(unit, &ineligible);
    auto& state = g_halo2FireEye;
    AcquireSRWLockExclusive(&state.lock);
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        auto& d = state.diag;
        ++d.samples[source >= 0 && source < Halo2FireEyeDiag::kSources ? source : 0];
        if (!read)
            ++(notRead == 1 ? d.noGeneration : notRead == 2 ? d.noPublication : notRead == 3 ? d.otherGeneration
               : notRead == 4 ? d.vehicle : d.stale);
        else if (!eligible)
            ++(ineligible == 1 ? d.notOwned : ineligible == 2 ? d.aimNotActive : ineligible == 3 ? d.notArmed
               : d.noStorage);
        else if (state.proof.pathKnown && path != state.proof.path)
            ++d.otherPath;
        else if (!publication.engineEyeVelValid)
            ++d.velocityUnknown;
        else if (!atRest)
            ++d.moving;
        else if (!std::isfinite(offsetMm))
            ++d.restNonFinite;
        else
        {
            d.restMinMm = d.restN ? std::min(d.restMinMm, double(offsetMm)) : double(offsetMm);
            d.restMaxMm = d.restN ? std::max(d.restMaxMm, double(offsetMm)) : double(offsetMm);
            ++d.restN;
            d.restSumMm += offsetMm;
            ++(offsetMm < shot_lag::EyeProof::kAgreeMm ? d.restAgree
               : offsetMm > shot_lag::EyeProof::kDisagreeMm ? d.restDisagree : d.restNeither);
        }
        int slot = -1;
        for (int i = 0; i < 4 && slot < 0; ++i)
            if (d.pathCount[i] && d.paths[i] == path)
                slot = i;
        for (int i = 0; i < 4 && slot < 0; ++i)
            if (!d.pathCount[i])
                slot = i;
        if (slot >= 0)
        {
            d.paths[slot] = path;
            ++d.pathCount[slot];
        }
    }
#else
    (void)source;
#endif
    state.proof.Note(generation, static_cast<int32_t>(unit), eligible, atRest, offsetMm, path);
#if HALOMCCVR_DEV_DIAGNOSTICS
    state.diag.bestStreak = std::max(state.diag.bestStreak, state.proof.proof.agree);
#endif
    const shot_lag::EyeProof::State now = state.proof.proof.state;
    const bool pathKnown = state.proof.pathKnown;
    const int32_t latched = state.proof.path;
    const bool changed = now != state.reportedState || pathKnown != state.reportedPath;
    state.reportedState = now;
    state.reportedPath = pathKnown;
    ReleaseSRWLockExclusive(&state.lock);
    if (changed)
        LOG("Halo 2 fire eye (flat parity): the firing assist's camera evaluator is %s%s (perspective %s%d); a "
            "muzzle shot's eye at the fire is %s", now == shot_lag::EyeProof::State::Proven ? "PROVEN"
            : now == shot_lag::EyeProof::State::Disproven ? "DISPROVEN" : "unproven",
            now == shot_lag::EyeProof::State::Proven ? " to be the engine eye at rest" : "",
            pathKnown ? "" : "not yet latched, last ", pathKnown ? latched : path,
            now == shot_lag::EyeProof::State::Proven ? "measured (that evaluator asked at the fire)"
                                                     : "the newest publication's eye carried one tick");
}

// The evaluator asked for `unit` now, on the fire thread, under SEH with the lane's callback count held (the
// trampoline cannot be retired under it: RemoveHalo2DualAim's quiescence). Returns false when it could not be asked.
bool Halo2CallFireEyeEvaluator(uint32_t unit, float position[3], int32_t& path)
{
    auto& feature = g_halo2Dual;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool called = false;
    __try
    {
        const Halo2DualCameraFn evaluator = feature.cameraOriginal;
        if (evaluator && feature.enabled.load(std::memory_order_acquire) &&
            !feature.faulted.load(std::memory_order_acquire) &&
            feature.generation == g_generation.load(std::memory_order_acquire))
        {
            float direction[3]{};
            path = evaluator(unit, position, direction);
            called = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        called = false;
    }
    feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    return called;
}

// THE TICK FEED (every build where the transport is wanted): our owned on-foot unit's own tick (the
// native aim update, simulation thread, the controller owning its aim), while its eye stands still: one proof sample
// from the evaluator, so the proof can latch before any shot. Moving, nothing is asked (a moving sample is no
// evidence). At rest the evaluator's tick-time answer and the publication's interpolated eye are the same point by
// construction, which is exactly what the proof checks; its interpolator write is the same value the players update
// writes later in this tick.
void Halo2NoteFireEyeTick(uint32_t unit) noexcept
{
    // Never in a shared simulation (Sight_FireEyeAsked): the ask is a call of the game's own inside the tick.
    if (!Sight_FireEyeAsked(GameTitle::Halo2) || unit == UINT32_MAX || unit != Halo2OwnedUnit())
        return;
    Halo2ObserverPosePublication publication{};
    int notRead = 0;
    if (!Halo2FireEyePublication(publication, g_generation.load(std::memory_order_acquire), Sight_NowUs(), notRead) ||
        !shot_lag::EyeAtRest(publication.engineEyeVelValid, publication.engineEyeVel))
        return;
    float position[3]{};
    int32_t path = 0;
    if (!Halo2CallFireEyeEvaluator(unit, position, path))
        return;
    Halo2NoteFireEyeProof(unit, shot_lag::Finite3(position) ? position : nullptr, path,
#if HALOMCCVR_DEV_DIAGNOSTICS
                          Halo2FireEyeDiag::kTick
#else
                          0
#endif
    );
}

// The evaluator asked inside this fire: its verdict, and its answer in `answer` whenever it answered finitely (dev:
// measured against the carried eye even while unproven).
shot_lag::FireEye Halo2AskFireEye(float eye[3], float answer[3], bool& answered)
{
    answered = false;
    const uint32_t unit = Halo2OwnedUnit();
    if (!Halo2FireEyeEligible(unit))
        return shot_lag::FireEye::OtherUnit;
    float position[3]{};
    int32_t path = 0;
    if (!Halo2CallFireEyeEvaluator(unit, position, path))
        return shot_lag::FireEye::Faulted;
    const bool finite = shot_lag::Finite3(position);
    Halo2NoteFireEyeProof(unit, finite ? position : nullptr, path,
#if HALOMCCVR_DEV_DIAGNOSTICS
                          Halo2FireEyeDiag::kShotAsk
#else
                          0
#endif
    );   // our own ask is a sample too
    if (finite)
    {
        std::memcpy(answer, position, sizeof(position));
        answered = true;
    }
    auto& state = g_halo2FireEye;
    AcquireSRWLockShared(&state.lock);
    const shot_lag::FireEye verdict = state.proof.Verdict(path, finite);
    ReleaseSRWLockShared(&state.lock);
    if (verdict == shot_lag::FireEye::Measured)
        std::memcpy(eye, position, sizeof(position));
    return verdict;
}

#if HALOMCCVR_DEV_DIAGNOSTICS
// MCC's frame-interpolator factor now (0x164B300, E-H2-30) and the game's own tick length (game_time +0x04), read
// guarded; NaN when unreadable. At a fire (inside a tick) the factor is still the one the newest publication was
// interpolated at: it is rewritten only after the frame's ticks.
void Halo2FireEyeClock(float& alpha, float& tickMs)
{
    alpha = tickMs = std::numeric_limits<float>::quiet_NaN();
    const uintptr_t base = g_halo2Dual.base;
    if (!base)
        return;
    __try
    {
        const float factor = *reinterpret_cast<const float*>(base + kHalo2FrameInterpolatorFactorRva);
        if (std::isfinite(factor) && factor >= 0.0f && factor <= 1.0f)
            alpha = factor;
        const uint8_t* time = *reinterpret_cast<const uint8_t* const*>(base + kHalo2GameTimeSlotRva);
        if (time && time[kHalo2GameTimeInitializedOffset] == 1)
        {
            float seconds = 0.0f;
            std::memcpy(&seconds, time + kHalo2GameTimeSecondsPerTickOffset, sizeof(seconds));
            if (std::isfinite(seconds) && seconds > 0.001f && seconds < 0.2f)
                tickMs = seconds * 1000.0f;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        alpha = tickMs = std::numeric_limits<float>::quiet_NaN();
    }
}

// The fire's two eyes against E_P (dev): along the eye's travel (its velocity), mm, and as time at that speed.
void Halo2NoteFireEyeBothWays(const SightPointSample& sample, const float answer[3], const float carried[3],
                              bool carriedValid)
{
    if (!sample.camera.valid || !sample.camera.velValid || !shot_lag::Finite3(sample.camera.vel) ||
        !shot_lag::Finite3(answer))
        return;
    const float speed = shot_lag::Length(sample.camera.vel);   // wu/s
    const float* e = sample.camera.eye;
    const float d[3]{answer[0] - e[0], answer[1] - e[1], answer[2] - e[2]};
    float alpha = 0.0f, tickMs = 0.0f;
    Halo2FireEyeClock(alpha, tickMs);
    auto& state = g_halo2FireEye;
    AcquireSRWLockExclusive(&state.lock);
    auto& diag = state.diag;
    ++diag.fires;
    if (std::isfinite(alpha))
    {
        ++diag.alphaN;
        diag.alphaSum += alpha;
    }
    if (std::isfinite(tickMs))
    {
        ++diag.tickMsN;
        diag.tickMsSum += tickMs;
    }
    if (speed * shot_lag::kMetresPerWu >= 1.0f)
    {
        const float unit[3]{sample.camera.vel[0] / speed, sample.camera.vel[1] / speed, sample.camera.vel[2] / speed};
        ++diag.firesMoving;
        const float evalWu = shot_lag::Dot(d, unit);
        diag.evalAlongMm += evalWu * shot_lag::kMmPerWu;
        diag.evalMs += evalWu / speed * 1000.0f;
        if (carriedValid && shot_lag::Finite3(carried))
        {
            const float c[3]{carried[0] - e[0], carried[1] - e[1], carried[2] - e[2]};
            const float carriedWu = shot_lag::Dot(c, unit);
            diag.carriedAlongMm += carriedWu * shot_lag::kMmPerWu;
            diag.carriedMs += carriedWu / speed * 1000.0f;
        }
        if (std::isfinite(alpha) && std::isfinite(tickMs))
            diag.predictedMs += (2.0f - alpha) * tickMs;
    }
    ReleaseSRWLockExclusive(&state.lock);
}
#endif

shot_lag::FireEye Halo2FireEyeForShot(const float* siteEye, bool askEvaluator, const SightPointSample& sample,
                                      float eye[3], float& ageMs)
{
    (void)siteEye;   // Halo 2's muzzle shot holds no eye of its own
    ageMs = -1.0f;
    if (TitleAdapter_GetActiveTitle() != GameTitle::Halo2)
        return shot_lag::FireEye::NoSource;
    float measured[3]{}, answer[3]{};
    bool answered = false;
    const shot_lag::FireEye asked =
        askEvaluator ? Halo2AskFireEye(measured, answer, answered) : shot_lag::FireEye::Count;
    // 60 Hz while unmeasured (MCC runs Halo 2's simulation at 60 Hz: HALO2-SIGNATURE-EVIDENCE.md E-H2-28).
    const float tickS = float(Sight_TickPeriodUs(GameTitle::Halo2, 16667)) * 1.0e-6f;
    const uint64_t nowUs = Sight_NowUs();
    const float carryS = shot_lag::SampleCarrySeconds(sample.camera.stampUs, nowUs, tickS, kSightTransportMaxAgeUs);
    // The dev A/B (halo2_measured_fire_eye, a player build always 1): 0 sets a measured eye aside, so the shot carries
    // the publication's eye one tick, as it does while the proof is unlatched.
    const shot_lag::FireEye admitted =
        asked == shot_lag::FireEye::Measured && !g_config.halo2_measured_fire_eye ? shot_lag::FireEye::Unproven : asked;
    const shot_lag::FireEye verdict = shot_lag::ChooseSampleFireEye(
        admitted, measured, sample.camera.valid, sample.camera.eye, sample.camera.velValid ? sample.camera.vel : nullptr,
        carryS, eye);
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Both ways, every fire the evaluator answered: its answer, and the eye the fallback would carry.
    if (answered)
    {
        float carried[3]{};
        const bool carriedValid = shot_lag::ChooseSampleFireEye(
            shot_lag::FireEye::Count, nullptr, sample.camera.valid, sample.camera.eye,
            sample.camera.velValid ? sample.camera.vel : nullptr, carryS, carried) == shot_lag::FireEye::CarriedTick;
        Halo2NoteFireEyeBothWays(sample, answer, carried, carriedValid);
    }
#else
    (void)answer;
    (void)answered;
#endif
    if (verdict == shot_lag::FireEye::Measured)
        ageMs = 0.0f;
    else if (verdict == shot_lag::FireEye::CarriedTick && sample.camera.stampUs && nowUs >= sample.camera.stampUs)
        ageMs = float(nowUs - sample.camera.stampUs) / 1000.0f;
    return verdict;
}
const bool g_halo2FireEyeRegistered = (Sight_SetFireEyeSource(GameTitle::Halo2, &Halo2FireEyeForShot), true);

// Cold worker (ReportTelemetry): the dev HALO 2 FIRE EYE line -- why the proof is where it is, counted.
void ReportHalo2FireEye()
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    auto& state = g_halo2FireEye;
    AcquireSRWLockExclusive(&state.lock);
    const Halo2FireEyeDiag d = state.diag;
    state.diag = Halo2FireEyeDiag{};
    const shot_lag::EyeProof::State proofState = state.proof.proof.state;
    const bool pathKnown = state.proof.pathKnown;
    const int32_t path = state.proof.path;
    const uint32_t streak = state.proof.proof.agree;
    ReleaseSRWLockExclusive(&state.lock);
    const uint32_t samples = d.samples[0] + d.samples[1] + d.samples[2];
    if (!samples && !d.fires)
        return;
    char paths[96]{};
    int used = 0;
    for (int i = 0; i < 4; ++i)
        if (d.pathCount[i] && used >= 0 && used < static_cast<int>(sizeof(paths)))
            used += std::snprintf(paths + used, sizeof(paths) - used, "%s%d x%u", used ? ", " : "", d.paths[i],
                                  d.pathCount[i]);
    const double meanMoving = d.firesMoving ? 1.0 / d.firesMoving : 0.0;
    LOG("HALO 2 FIRE EYE (dev): proof %s, perspective %s%d, streak now %u (best %u) of %u at rest within %.0f mm; "
        "samples %u (firing assist %u, shot ask %u, tick %u), perspectives seen [%s]; not counted: no generation %u, no "
        "publication %u, another generation %u, seated %u, stale publication %u, not our unit %u, aim not the "
        "controller's %u, not armed %u, no target storage %u, on another perspective %u, eye velocity unknown %u, "
        "moving %u; AT REST %u: off E_P avg %.0f mm (min %.0f, max %.0f), agree %u, between %u, disagree %u, "
        "non-finite %u. FIRES %u (%u moving over 1 m/s): along the eye's travel from E_P the evaluator answered %+.0f "
        "mm (%+.1f ms), the carried eye %+.0f mm (%+.1f ms); the interpolator predicts (2 - factor) ticks = %.1f ms "
        "(factor avg %.2f, game tick %.2f ms)",
        proofState == shot_lag::EyeProof::State::Proven ? "PROVEN"
        : proofState == shot_lag::EyeProof::State::Disproven ? "DISPROVEN" : "unproven",
        pathKnown ? "" : "unlatched ", pathKnown ? path : -1, streak, d.bestStreak, shot_lag::EyeProof::kAgreeToProve,
        static_cast<double>(shot_lag::EyeProof::kAgreeMm), samples, d.samples[0], d.samples[1], d.samples[2], paths,
        d.noGeneration, d.noPublication, d.otherGeneration, d.vehicle, d.stale, d.notOwned, d.aimNotActive,
        d.notArmed, d.noStorage, d.otherPath, d.velocityUnknown, d.moving, d.restN,
        d.restN ? d.restSumMm / d.restN : 0.0, d.restMinMm, d.restMaxMm, d.restAgree, d.restNeither, d.restDisagree,
        d.restNonFinite, d.fires, d.firesMoving, d.evalAlongMm * meanMoving, d.evalMs * meanMoving,
        d.carriedAlongMm * meanMoving, d.carriedMs * meanMoving, d.predictedMs * meanMoving,
        d.alphaN ? d.alphaSum / d.alphaN : 0.0, d.tickMsN ? d.tickMsSum / d.tickMsN : 0.0);
#endif
}
