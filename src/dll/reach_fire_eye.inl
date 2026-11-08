// Included in game.cpp (anonymous namespace) after reach_sight_trace.inl.
//
// FLAT PARITY, Halo Reach on foot (sight_layer.h SightFireEyeFn; shot_lag_logic.h 4f). Reach's on-foot
// shot sites aim through the provider's actuated WORLD point P: the unit-adjust re-aim and the muzzle shot from their
// own origins, the firing assist's crosshair query from its own start. Stock flat Halo fires along the crosshair
// direction from the eye AFTER the fire tick's move, so a site that aims through a fixed P trails a strafe by
// E_fire - E_P -- unless Reach fires from the eye the render showed. So each such shot aims through
// P' = P + (E_fire - E_P), with E_fire MEASURED or not at all:
//
//   E_P     the engine eye the provider solved from (the local shot ray's `base`, stamped at its publish);
//   E_fire  Reach's own unit camera-position evaluator (HREK 0xD76E60, retail 0x48A1C0: the trampoline, never our
//           hook), asked once inside the native fire for OUR unit, after the tick's move. It is the camera the
//           unit-adjust builds the native origin on (R-V12: origin := camera + forward * dot(origin - camera,
//           forward)); asked at the same instant, it answers the same eye.
//
// Its answer counts only once PROVEN (shot_lag::EvaluatorEyeProof): at rest, where the two are the same point by
// construction, it must agree with E_P within 5 cm for a streak of 30 samples. The samples come from the trace tick
// (ReachSightTraceTick: the per-unit update the aim trace already runs in, ~30 ms, our unit, on foot), so no shot is
// needed to prove it. Until proven -- or if disproven, or the evaluator is not installed on this build -- the shot
// keeps P (SightPointSample::measuredOnly): never carried a tick, never extrapolated (the rule: leave Reach
// unshifted rather than carry blindly). If Reach does fire from the render's eye, the measured shift reads ~0 on the
// dev SHOT LAG transport lines, and the transport is a no-op.
//
// The direct drive's record is a DIRECTION solved from E_P (desired_angles); applied by Reach at its own eye at the
// fire it already passes through P', so it is not shifted. Seats are not covered (Sight_TransportWanted).

// Reach's evaluator answers one camera on foot (seated it reads the seat's camera marker; a seat is never eligible), so
// its proof latches this single path; identity (our unit, on foot, alive in gameplay, no cinematic) is the guard.
inline constexpr int32_t kReachFireEyePath = 0;

#if HALOMCCVR_DEV_DIAGNOSTICS
// Why a proof sample did or did not count, and the fire measured both ways (dev, per report window; under the lock).
// THE INTERPOLATED CAMERA (static analysis of the pinned haloreach.dll; as_seen_logic.h section 8): the
// unit camera position 0x48A1C0 -> 0x4A2D50 reads the unit's origin back as lerp(previous tick, newest tick, factor)
// outside a tick (0x4719C8; factor = clamp(game_time + 0x14, 0, 1), written at 0x344A5 after the frame's ticks, right
// before the director update 0xC4458 that builds the first-person camera 0x1DCADC -> 0x48A4B4 -> 0x48A1C0) and its
// camera height the same way (0xCF408); inside a tick it answers that tick's raw eye. So E_P (the stock camera the
// render showed) is (1 - factor) of a tick behind the newest tick's eye, and a fire after its tick's move leaves from
// (2 - factor) ticks of travel past E_P. The evaluator asked inside the fire answers exactly that eye, so the measured
// shift is right whatever the factor; the line below compares it with a one-tick carry and with the prediction.
struct ReachFireEyeDiag
{
    uint32_t samples = 0, seated = 0, cinematic = 0, noIntent = 0, noStamp = 0, stale = 0, velocityUnknown = 0,
             moving = 0, evaluatorFailed = 0;
    uint32_t restN = 0, restAgree = 0, restNeither = 0, restDisagree = 0;
    double restSumMm = 0.0, restMinMm = 0.0, restMaxMm = 0.0;
    uint32_t bestStreak = 0;
    uint32_t lineN = 0;
    double lineSumMm = 0.0, lineMaxMm = 0.0;
    as_seen::FireEyeBothWays fires{};
    uint32_t fireNoSample = 0;
};
#endif

struct ReachFireEyeState
{
    SRWLOCK lock = SRWLOCK_INIT;
    shot_lag::EvaluatorEyeProof proof{};
    // Every build, per report window: asks inside a fire, and asks that could not reach the evaluator.
    std::atomic<uint32_t> asks{0}, unanswered{0};
#if HALOMCCVR_DEV_DIAGNOSTICS
    ReachFireEyeDiag diag{};
#endif
} g_reachFireEye;

#if HALOMCCVR_DEV_DIAGNOSTICS
void ReachClockRead(int64_t& tick, float& factor, float* tickMs);   // reach_impact_witness.inl
#endif

// The stock evaluator for `unit` (the trampoline), guarded. Callers hold a lane's callback count that keeps the core
// trampolines alive: g_reachCamera.activeCallbacks (the unit-adjust hook, the per-unit update) or the muzzle lane's
// (retired before the core). Kept free of C++ objects so __try is legal.
bool ReachCallUnitCameraEvaluator(int32_t unit, float* out)
{
    const ReachUnitCameraPositionFn evaluator = g_origReachUnitCameraPosition;
    if (!evaluator || !out || unit == -1)
        return false;
    __try
    {
        evaluator(unit, out);
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// THE ASK IS A CALL OF THE GAME'S OWN INSIDE A TICK: inside the native fire, and for the proof inside the unit's
// update. In a simulation another machine shares (sight_layer.h Sight_FireTimeChangesAllowed false) only this
// machine makes those calls, and they are not without effect there: one saved co-op film replayed with the fire's
// ask parts from the game's own replay at a shot (two Grunts 1e-5 world units apart, then the vehicle the player
// boards, then which objects exist), and replayed without it does not
// (docs/COOP-SHARED-SIMULATION-2026-10-13.md). The shot is left as the game built it there anyway, so the eye would
// be measured for nothing. So there the evaluator is not asked: no proof sample, no fire eye, and every consumer
// keeps P (FireEye::NotRead).
bool ReachFireEyeAsked()
{
    return Sight_FireEyeAsked(GameTitle::HaloReach);
}

// Inside a native fire, on the fire thread: this fire's eye for `unit`, asked once (a later site of the same fire
// reuses it) and only for our own unit. `fresh`: no enclosing fire scope exists (the fire detour is absent), so the
// scope cannot be trusted to belong to this fire -- ask anew.
void ReachMeasureFireEye(int32_t unit, bool fresh)
{
    if (unit == -1 || !ReachFireEyeAsked() || unit != LegacyCollisionIgnoredObject(GameTitle::HaloReach))
        return;
    shot_lag::FireEyeScope& scope = t_reachFireEye;
    if (!fresh && scope.asked && scope.unit == unit)
        return;
    float eye[3]{};
    const bool called = ReachCallUnitCameraEvaluator(unit, eye);
    scope.Store(unit, called, called ? eye : nullptr);
    g_reachFireEye.asks.fetch_add(1, std::memory_order_relaxed);
    if (!called)
        g_reachFireEye.unanswered.fetch_add(1, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Both ways (dev), once per fire: this answer and E_P carried one tick against E_P, with the (2 - factor)
    // prediction. The factor read inside the tick is still the one the newest render's camera was built at (it is
    // rewritten only after the frame's ticks).
    if (called)
    {
        SightPointSample sample{};
        uint64_t atUs = 0;
        const bool read = ReachSightReadIntentSample(kSightIntentShotMaxAgeMs * 1000ull, sample, atUs) &&
                          sample.camera.valid;
        int64_t tick = -1;
        float factor = NAN, tickMs = NAN;
        ReachClockRead(tick, factor, &tickMs);
        float carried[3]{};
        bool carriedValid = false;
        if (read)
        {
            const float tickS = std::isfinite(tickMs) ? tickMs * 1.0e-3f
                                                      : float(Sight_TickPeriodUs(GameTitle::HaloReach, 16667)) * 1.0e-6f;
            carriedValid = shot_lag::ChooseSampleFireEye(
                               shot_lag::FireEye::Count, nullptr, true, sample.camera.eye,
                               sample.camera.velValid ? sample.camera.vel : nullptr,
                               shot_lag::SampleCarrySeconds(sample.camera.stampUs, Sight_NowUs(), tickS,
                                                            kSightTransportMaxAgeUs),
                               carried) == shot_lag::FireEye::CarriedTick;
        }
        auto& state = g_reachFireEye;
        AcquireSRWLockExclusive(&state.lock);
        if (!read)
            ++state.diag.fireNoSample;
        else
            state.diag.fires.Add(sample.camera.eye, sample.camera.velValid ? sample.camera.vel : nullptr, eye,
                                 carriedValid ? carried : nullptr, factor, tickMs);
        ReleaseSRWLockExclusive(&state.lock);
    }
#endif
}

// One proof sample from the trace tick (sim thread, inside the per-unit update, our unit only, ~30 ms): E_P from the
// freshest intent (its camera sample under 100 ms old), at rest only; the evaluator is asked only then. Every build.
void ReachNoteFireEyeProof(int32_t localUnit, bool onFoot, uint32_t generation)
{
    if (!g_origReachUnitCameraPosition || !ReachFireEyeAsked())
        return;
    SightPointSample sample{};
    uint64_t atUs = 0;
    const uint64_t nowUs = Sight_NowUs();
    const bool readIntent = onFoot && !g_reachCinematicLocked.load(std::memory_order_relaxed) &&
        ReachSightReadIntentSample(100000ull, sample, atUs);
    const bool read = readIntent && sample.camera.valid &&
        nowUs >= sample.camera.stampUs && nowUs - sample.camera.stampUs <= 100000ull;
    const bool atRest = read && shot_lag::EyeAtRest(sample.camera.velValid, sample.camera.vel);
    float offsetMm = std::numeric_limits<float>::quiet_NaN();
    bool eligible = read;
    if (atRest)
    {
        float eye[3]{};
        eligible = ReachCallUnitCameraEvaluator(localUnit, eye);
        if (eligible)
        {
            const float d[3]{eye[0] - sample.camera.eye[0], eye[1] - sample.camera.eye[1],
                             eye[2] - sample.camera.eye[2]};
            offsetMm = shot_lag::Length(d) * shot_lag::kMmPerWu;
        }
    }
    auto& state = g_reachFireEye;
    AcquireSRWLockExclusive(&state.lock);
    state.proof.Note(generation, localUnit, eligible, atRest, offsetMm, kReachFireEyePath);
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        // Every sample, and the one reason it did or did not count (in the order the gates run).
        auto& d = state.diag;
        ++d.samples;
        if (!onFoot)
            ++d.seated;
        else if (g_reachCinematicLocked.load(std::memory_order_relaxed))
            ++d.cinematic;
        else if (!readIntent)
            ++d.noIntent;
        else if (!sample.camera.valid)
            ++d.noStamp;
        else if (!read)
            ++d.stale;
        else if (!sample.camera.velValid)
            ++d.velocityUnknown;
        else if (!atRest)
            ++d.moving;
        else if (!eligible || !std::isfinite(offsetMm))
            ++d.evaluatorFailed;
        else
        {
            d.restMinMm = d.restN ? std::min(d.restMinMm, double(offsetMm)) : double(offsetMm);
            d.restMaxMm = d.restN ? std::max(d.restMaxMm, double(offsetMm)) : double(offsetMm);
            ++d.restN;
            d.restSumMm += offsetMm;
            ++(offsetMm < shot_lag::EyeProof::kAgreeMm ? d.restAgree
               : offsetMm > shot_lag::EyeProof::kDisagreeMm ? d.restDisagree : d.restNeither);
        }
        d.bestStreak = std::max(d.bestStreak, state.proof.proof.agree);
    }
#endif
    ReleaseSRWLockExclusive(&state.lock);
}

// Dev witness (the unit-adjust, after the native call built origin + direction): how far this fire's eye lies off the
// native shot line. Reach builds its origin on the evaluator's camera line, so the answer that is this fire's camera
// sits ~0 off it (a barrel offset aside); the solve's eye E_P sits off it by the eye's travel since the render.
void ReachNoteFireEyeNativeLine(int32_t unit, const float* origin, const float* direction)
{
#if HALOMCCVR_DEV_DIAGNOSTICS
    float eye[3]{};
    if (!origin || !direction || t_reachFireEye.For(unit, eye) != shot_lag::FireEye::Measured)
        return;
    const float offWu = shot_lag::PointLineDistance(eye, origin, direction);
    if (!std::isfinite(offWu))
        return;
    auto& state = g_reachFireEye;
    const double mm = double(offWu) * shot_lag::kMmPerWu;
    AcquireSRWLockExclusive(&state.lock);
    ++state.diag.lineN;
    state.diag.lineSumMm += mm;
    state.diag.lineMaxMm = std::max(state.diag.lineMaxMm, mm);
    ReleaseSRWLockExclusive(&state.lock);
#else
    (void)unit; (void)origin; (void)direction;
#endif
}

// THE FIRE EYE (SightFireEyeFn), on the fire thread: this fire's evaluator answer for our unit, Measured only while
// proven on this generation; anything else is returned as the reason P stays (counted on the transport line).
shot_lag::FireEye ReachFireEyeForShot(const float* siteEye, bool askEvaluator, const SightPointSample& sample,
                                      float eye[3], float& ageMs)
{
    (void)siteEye; (void)askEvaluator; (void)sample;   // the fire's own ask (ReachMeasureFireEye) is the only source
    ageMs = -1.0f;
    if (TitleAdapter_GetActiveTitle() != GameTitle::HaloReach || !g_origReachUnitCameraPosition)
        return shot_lag::FireEye::NoSource;
    float answer[3]{};
    shot_lag::FireEye asked = t_reachFireEye.For(LegacyCollisionIgnoredObject(GameTitle::HaloReach), answer);
    if (asked == shot_lag::FireEye::Measured)
    {
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        auto& state = g_reachFireEye;
        AcquireSRWLockShared(&state.lock);
        asked = state.proof.proof.generation != generation ? shot_lag::FireEye::Unproven
                                                           : state.proof.Verdict(kReachFireEyePath, true);
        ReleaseSRWLockShared(&state.lock);
    }
    const shot_lag::FireEye verdict = shot_lag::ChooseMeasuredFireEye(asked, answer, eye);
    if (verdict == shot_lag::FireEye::Measured)
        ageMs = 0.0f;   // asked inside this fire
    return verdict;
}
const bool g_reachFireEyeRegistered = (Sight_SetFireEyeSource(GameTitle::HaloReach, &ReachFireEyeForShot), true);

// Cold worker (ReportReachSightTrace): the proof's state when it changes (every build: rare and load-bearing), a loud
// line when this build has no evaluator to measure with, and the dev witness.
void ReportReachFireEye()
{
    auto& state = g_reachFireEye;
    const bool wanted = ReachFireEyeAsked();
    // Said when it changes: whether the eye is measured at all (ReachFireEyeAsked).
    static int s_lastAsked = -1;
    if (Sight_TransportWanted(GameTitle::HaloReach, false) && int(wanted) != s_lastAsked)
    {
        if (!wanted)
            LOG("Reach fire eye (flat parity): not measured while Co-op safe is on (the measurement is a call of "
                "the game's own inside a simulation tick, and there the shot is left as the game built it)");
        else if (s_lastAsked == 0)
            LOG("Reach fire eye (flat parity): measured again (Co-op safe is off)");
        s_lastAsked = int(wanted);
    }
    static int s_lastAvailable = -1;
    const int available = g_origReachUnitCameraPosition ? 1 : 0;
    if (wanted && available != s_lastAvailable)
    {
        if (!available)
            LOG("Reach fire eye (flat parity): the unit camera-position evaluator is NOT installed on this build, so "
                "no fire eye can be measured; Reach's on-foot shots keep aiming through the intent point P unmoved "
                "(no carried or extrapolated eye is used instead)");
        else if (s_lastAvailable == 0)
            LOG("Reach fire eye (flat parity): the unit camera-position evaluator is installed again");
        s_lastAvailable = available;
    }
    AcquireSRWLockShared(&state.lock);
    const shot_lag::EyeProof::State proofState = state.proof.proof.state;
    const bool pathKnown = state.proof.pathKnown;
    ReleaseSRWLockShared(&state.lock);
    static shot_lag::EyeProof::State s_reported = shot_lag::EyeProof::State::Unproven;
    static bool s_reportedPath = false;
    if (proofState != s_reported || pathKnown != s_reportedPath)
    {
        LOG("Reach fire eye (flat parity): the unit camera-position evaluator is %s; an on-foot shot's eye at the "
            "fire is %s",
            proofState == shot_lag::EyeProof::State::Proven ? "PROVEN to be the engine eye at rest"
            : proofState == shot_lag::EyeProof::State::Disproven ? "DISPROVEN (it disagrees with the engine eye at "
                                                                    "rest)"
                                                                  : "unproven",
            proofState == shot_lag::EyeProof::State::Proven ? "measured (that evaluator, asked in the fire)"
                                                            : "not measured: the shot keeps the intent point P");
        s_reported = proofState;
        s_reportedPath = pathKnown;
    }
    const uint32_t asks = state.asks.exchange(0, std::memory_order_relaxed);
    const uint32_t unanswered = state.unanswered.exchange(0, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    AcquireSRWLockExclusive(&state.lock);
    const ReachFireEyeDiag d = state.diag;
    state.diag = ReachFireEyeDiag{};
    const uint32_t streak = state.proof.proof.agree;
    ReleaseSRWLockExclusive(&state.lock);
    if (!asks && !d.samples && !d.fires.fires)
        return;
    char both[640]{};
    if (!d.fires.Format(both, sizeof(both)))
        std::snprintf(both, sizeof(both), "FIRES 0");
    LOG("REACH FIRE EYE (dev): proof %s, streak now %u (best %u) of %u at rest within %.0f mm; %u asked in a fire (%u "
        "unanswered); off the native shot line avg %.0f / max %.0f mm (%u unit-adjust shots: ~0 = the camera the native "
        "origin is built on); proof samples %u -- not counted: seated %u, cinematic %u, no intent %u, no camera stamp "
        "%u, stale %u, eye velocity unknown %u, moving %u, evaluator failed %u; AT REST %u: off E_P avg %.0f mm (min "
        "%.0f, max %.0f), agree %u, between %u, disagree %u. %s (%u fires without an intent sample)",
        proofState == shot_lag::EyeProof::State::Proven ? "PROVEN"
        : proofState == shot_lag::EyeProof::State::Disproven ? "DISPROVEN" : "unproven",
        streak, d.bestStreak, shot_lag::EyeProof::kAgreeToProve, static_cast<double>(shot_lag::EyeProof::kAgreeMm),
        asks, unanswered, d.lineN ? d.lineSumMm / d.lineN : 0.0, d.lineMaxMm, d.lineN, d.samples, d.seated,
        d.cinematic, d.noIntent, d.noStamp, d.stale, d.velocityUnknown, d.moving, d.evaluatorFailed, d.restN,
        d.restN ? d.restSumMm / d.restN : 0.0, d.restMinMm, d.restMaxMm, d.restAgree, d.restNeither, d.restDisagree,
        both, d.fireNoSample);
#else
    (void)asks; (void)unanswered;
#endif
}
