// Included in game.cpp (anonymous namespace) after halo4_muzzle_lifecycle.inl.
//
// FLAT PARITY, Halo 4 on foot (sight_layer.h SightFireEyeFn; shot_lag_logic.h 4e; the fire eye is MEASURED). Halo 4's
// muzzle shot aims through the intent point P (Sight_AimMuzzleShot), a world point fixed at the controller root the
// solve read: E_P, the stock observer camera the render showed (the frame before the HMD pose is composed onto it). A
// stock shot leaves from the eye AFTER its tick's move, so the shot flies through P' = P + (E_fire - E_P).
//
// WHY A ONE-TICK CARRY IS SHORT IN HALO 4 (static analysis of the pinned halo4.dll; as_seen_logic.h section
// 8). Halo 4 runs MCC's frame interpolator exactly as Halo Reach does: the tick loop 0x9A380 sets the interpolator's
// in-tick byte (state +3) before every tick; after the frame's ticks the frame function writes the blend factor
// clamp(game_time + 0x14, 0, 1) (0xA0458) into state +4 at 0x61172, and only then runs the director update 0x10A448,
// whose first-person camera reaches the unit camera position 0x5F8A1C (via 0x5F8E88). Outside a tick that position is
// the unit's origin read back as lerp(previous tick, newest tick, factor) (0x6320D0 -> 0x5D7E08) plus its interpolated
// camera height (0x1343E4); inside a tick it is that tick's raw eye (and records the height, 0x134330). So E_P is
// (1 - factor) of a tick behind the newest tick's eye, and a fire after its tick's move leaves from (2 - factor) ticks
// of travel past it: a one-tick carry falls short by (1 - factor) of a tick -- about half a tick on average,
// ~50 mm at 6 m/s -- exactly as in Halo 2 (shot-placement.md section 3).
//
// THE FIX, as Halo 2's: E_fire is MEASURED. The fire routine 0x6176B8 itself asks the unit camera position 0x5F8A1C for
// its shooter (its call at 0x617FBF), inside the tick, after the unit's move; asked by us for our unit inside the same
// fire, it answers the eye the round leaves from. Its answer counts only once PROVEN (shot_lag::EvaluatorEyeProof): at
// rest -- where it and E_P are the same point by construction -- it must agree with E_P within 5 cm for a streak of 30
// samples. The samples come from the unit's own tick while it stands still (Halo4NoteFireEyeTick, from the per-unit
// update the contact lane hooks), NOT only from fires: a proof fed only at fires starves, because the player
// strafes whenever he fires. Until proven (or if disproven, or the evaluator did not resolve) the shot keeps the
// fallback model, E_P carried one measured tick (shot_lag::ChooseSampleFireEye). Every verdict is counted on the dev
// SHOT LAG transport line; the dev HALO 4 FIRE EYE line counts every proof sample and measures each fire both ways
// against the interpolator's (2 - factor) prediction. Dev A/B: halo4_measured_fire_eye (a player build always 1).
//
// SAFE TO ASK: `py -3 tools/audit_caller_area.py halo4.dll 5F8A1C:2:v` -- no caller-area slot touched, 42
// native callers, none relies on a volatile register. Inside a tick it writes only its user's camera-height slot of the
// current interpolator bank (0x134330), the same value the game's own per-tick aim camera call writes later in the same
// tick (the game tick 0x9A51C runs the objects update and the fire before its aim-camera calls at 0x9AA33 / 0x9AA47), so
// asking it never changes what the renderer interpolates. It is called only from inside the contact lane's per-unit
// detour or the muzzle lane's fire detours, whose quiescence the H4 teardown waits for before the module is released.
// ADDR-HYGIENE: guarded (unique entry signature at its pinned RVA plus the fire's own call edge; the answer is proven
// against E_P before it is ever used).

using Halo4UnitCameraPositionFn = void(__fastcall*)(int32_t unit, float* out);

inline constexpr int32_t kHalo4FireEyePath = 0;   // the evaluator answers one camera on foot (a seat is never eligible)

#if HALOMCCVR_DEV_DIAGNOSTICS
struct Halo4FireEyeDiag
{
    enum Source : int { kTick = 0, kShotAsk, kSources };
    uint32_t samples[kSources]{};
    uint32_t notOurs = 0, seated = 0, cinematic = 0, noIntent = 0, otherGeneration = 0, noStamp = 0, stale = 0,
             velocityUnknown = 0, moving = 0, evaluatorFailed = 0;
    uint32_t restN = 0, restAgree = 0, restNeither = 0, restDisagree = 0;
    double restSumMm = 0.0, restMinMm = 0.0, restMaxMm = 0.0;
    uint32_t bestStreak = 0;
    as_seen::FireEyeBothWays fires{};
    uint32_t verdicts[static_cast<int>(shot_lag::FireEye::Count) + 1]{};
};
#endif

struct Halo4FireEyeState
{
    std::atomic<Halo4UnitCameraPositionFn> evaluator{nullptr};
    std::atomic<uint32_t> generation{0};
    SRWLOCK lock = SRWLOCK_INIT;
    shot_lag::EvaluatorEyeProof proof{};
    shot_lag::EyeProof::State reportedState = shot_lag::EyeProof::State::Unproven;
    bool reportedPath = false;
    std::atomic<uint32_t> asks{0}, unanswered{0};
#if HALOMCCVR_DEV_DIAGNOSTICS
    Halo4FireEyeDiag diag{};
#endif
} g_halo4FireEye;

#if HALOMCCVR_DEV_DIAGNOSTICS
mcc_clock::Clock g_halo4Clock;   // MCC's frame clock in halo4.dll (dev)
void Halo4ClockRead(int64_t& tick, float& factor, float* tickMs = nullptr)
{
    (void)g_halo4Clock.Capture();   // a no-op off the game's threads
    g_halo4Clock.Read(tick, factor, tickMs);
}
#endif

// The stock evaluator for `unit`, guarded; false when it is not resolved, faulted, or answered a non-finite eye.
bool Halo4CallFireEyeEvaluator(int32_t unit, float out[3])
{
    const Halo4UnitCameraPositionFn evaluator = g_halo4FireEye.evaluator.load(std::memory_order_acquire);
    if (!evaluator || unit == -1 || !out)
        return false;
    out[0] = out[1] = out[2] = std::numeric_limits<float>::quiet_NaN();
    __try
    {
        evaluator(unit, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return shot_lag::Finite3(out);
}

// The newest intent's camera sample as a proof's E_P: this generation, on foot, stamped, fresh (100 ms). `reason` (dev):
// 1 none published, 2 another generation, 3 a seat's, 4 no stamp, 5 stale or from the future.
bool Halo4FireEyeSample(uint32_t generation, uint64_t nowUs, SightIntent& intent, int& reason)
{
    reason = 0;
    if (!Sight_ReadIntent(GameTitle::Halo4, intent))
        reason = 1;
    else if (intent.p.generation != generation)
        reason = 2;
    else if (intent.p.seated)
        reason = 3;
    else if (!intent.p.pointEyeValid || !intent.p.pointEyeStampUs)
        reason = 4;
    else if (nowUs < intent.p.pointEyeStampUs || nowUs - intent.p.pointEyeStampUs > 100000)
        reason = 5;
    return reason == 0;
}

// Our on-foot unit, in player control: the unit the render's first-person body was last solved for (fresh), not
// seated. `reason` (dev): 1 not ours, 2 seated, 3 cinematic.
bool Halo4FireEyeEligible(int32_t unit, int& reason)
{
    reason = 0;
    const uint64_t now = GetTickCount64();
    const uint64_t at = g_halo4SightUnitAtMs.load(std::memory_order_acquire);
    if (unit == -1 || !at || now < at || now - at > 250 || unit != g_halo4SightUnit.load(std::memory_order_relaxed))
        reason = 1;
    else
    {
        Halo4VehicleInputState seat{};
        if (!Halo4ReadVehicleInput(seat) || seat.unit != static_cast<uint32_t>(unit) || seat.seated)
            reason = 2;
        else if (ReadHalo4CinematicControl() != CinematicControlState::PlayerControlled)
            reason = 3;
    }
    return reason == 0;
}

// One proof sample: the evaluator's answer `answer` (null: none) for `unit` against the newest intent's E_P.
void Halo4NoteFireEyeProof(int32_t unit, bool eligible, int notEligible, const float* answer, int source)
{
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
    SightIntent intent{};
    int notRead = 0;
    const bool read = Halo4FireEyeSample(generation, Sight_NowUs(), intent, notRead);
    const bool atRest = read && shot_lag::EyeAtRest(intent.p.eyeVelValid, intent.p.eyeVel);
    float offsetMm = std::numeric_limits<float>::quiet_NaN();
    if (read && answer && shot_lag::Finite3(answer))
    {
        const float d[3]{answer[0] - intent.p.pointEye[0], answer[1] - intent.p.pointEye[1],
                         answer[2] - intent.p.pointEye[2]};
        offsetMm = shot_lag::Length(d) * shot_lag::kMmPerWu;
    }
    auto& state = g_halo4FireEye;
    AcquireSRWLockExclusive(&state.lock);
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        auto& d = state.diag;
        ++d.samples[source >= 0 && source < Halo4FireEyeDiag::kSources ? source : 0];
        if (!eligible)
            ++(notEligible == 2 ? d.seated : notEligible == 3 ? d.cinematic : d.notOurs);
        else if (!read)
            ++(notRead == 2 ? d.otherGeneration : notRead == 3 ? d.seated : notRead == 4 ? d.noStamp
               : notRead == 5 ? d.stale : d.noIntent);
        else if (!intent.p.eyeVelValid)
            ++d.velocityUnknown;
        else if (!atRest)
            ++d.moving;
        else if (!std::isfinite(offsetMm))
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
    }
#else
    (void)notEligible;
    (void)source;
#endif
    state.proof.Note(generation, unit, eligible && read, atRest, offsetMm, kHalo4FireEyePath);
#if HALOMCCVR_DEV_DIAGNOSTICS
    state.diag.bestStreak = std::max(state.diag.bestStreak, state.proof.proof.agree);
#endif
    const shot_lag::EyeProof::State now = state.proof.proof.state;
    const bool pathKnown = state.proof.pathKnown;
    const bool changed = now != state.reportedState || pathKnown != state.reportedPath;
    state.reportedState = now;
    state.reportedPath = pathKnown;
    ReleaseSRWLockExclusive(&state.lock);
    if (changed)
        LOG("Halo 4 fire eye (flat parity): the unit camera position (the evaluator the fire itself asks) is %s; a "
            "muzzle shot's eye at the fire is %s",
            now == shot_lag::EyeProof::State::Proven ? "PROVEN to be the engine eye at rest"
            : now == shot_lag::EyeProof::State::Disproven ? "DISPROVEN (it disagrees with the engine eye at rest)"
                                                          : "unproven",
            now == shot_lag::EyeProof::State::Proven ? "measured (that evaluator, asked in the fire)"
                                                     : "the controller root's eye carried one tick");
}

// THE TICK FEED (every build where the fire eye is asked for: Sight_FireEyeAsked, so never in a shared simulation --
// the ask is a call of the game's own inside the tick that only this machine would make): our on-foot unit's own
// update (the contact lane's per-unit detour, simulation thread), while its eye stands still -- one proof sample, so
// the proof latches before any shot. Moving, nothing is asked (a moving sample is no evidence). `onFoot`: the
// caller's biped test on the live instance.
void Halo4NoteFireEyeTick(uint32_t unit, bool onFoot)
{
    if (!onFoot || !Sight_FireEyeAsked(GameTitle::Halo4) ||
        !g_halo4FireEye.evaluator.load(std::memory_order_acquire))
        return;
#if HALOMCCVR_DEV_DIAGNOSTICS
    (void)g_halo4Clock.Capture();   // the game thread: the frame clock's pointers for the dev witnesses
#endif
    const int32_t local = static_cast<int32_t>(unit);
    int notEligible = 0;
    const bool eligible = Halo4FireEyeEligible(local, notEligible);
    if (!eligible && notEligible == 1)
        return;   // another unit's update: every unit comes through this hook
    // Ours: seated or in a cinematic ends the proof's stint (noted ineligible); on foot the evaluator is asked only at
    // rest (a moving answer is no evidence, and the proof ignores it).
    SightIntent intent{};
    int notRead = 0;
    const bool atRest = eligible &&
        Halo4FireEyeSample(TitleAdapter_GetGeneration(GameTitle::Halo4), Sight_NowUs(), intent, notRead) &&
        shot_lag::EyeAtRest(intent.p.eyeVelValid, intent.p.eyeVel);
    float answer[3]{};
    const bool answered = atRest && Halo4CallFireEyeEvaluator(local, answer);
    Halo4NoteFireEyeProof(local, eligible, notEligible, answered ? answer : nullptr,
#if HALOMCCVR_DEV_DIAGNOSTICS
                          Halo4FireEyeDiag::kTick
#else
                          0
#endif
    );
}

// The evaluator asked inside this fire for our unit: its verdict, and its answer in `answer` whenever it answered.
shot_lag::FireEye Halo4AskFireEye(float eye[3], float answer[3], bool& answered)
{
    answered = false;
    const int32_t local = g_halo4SightUnit.load(std::memory_order_relaxed);
    int notEligible = 0;
    if (!Halo4FireEyeEligible(local, notEligible))
        return shot_lag::FireEye::OtherUnit;
    g_halo4FireEye.asks.fetch_add(1, std::memory_order_relaxed);
    float position[3]{};
    if (!Halo4CallFireEyeEvaluator(local, position))
    {
        g_halo4FireEye.unanswered.fetch_add(1, std::memory_order_relaxed);
        return shot_lag::FireEye::Faulted;
    }
    Halo4NoteFireEyeProof(local, true, 0, position,
#if HALOMCCVR_DEV_DIAGNOSTICS
                          Halo4FireEyeDiag::kShotAsk
#else
                          1
#endif
    );   // our own ask is a sample too
    memcpy(answer, position, sizeof(position));
    answered = true;
    auto& state = g_halo4FireEye;
    AcquireSRWLockShared(&state.lock);
    const shot_lag::FireEye verdict = state.proof.Verdict(kHalo4FireEyePath, true);
    ReleaseSRWLockShared(&state.lock);
    if (verdict == shot_lag::FireEye::Measured)
        memcpy(eye, position, sizeof(position));
    return verdict;
}

// THE FIRE EYE (SightFireEyeFn), on the fire thread.
shot_lag::FireEye Halo4FireEyeForShot(const float* siteEye, bool askEvaluator, const SightPointSample& sample,
                                      float eye[3], float& ageMs)
{
    (void)siteEye;   // Halo 4's muzzle shot holds no eye of its own
    ageMs = -1.0f;
    if (TitleAdapter_GetActiveTitle() != GameTitle::Halo4)
        return shot_lag::FireEye::NoSource;
    float measured[3]{}, answer[3]{};
    bool answered = false;
    const shot_lag::FireEye asked =
        askEvaluator ? Halo4AskFireEye(measured, answer, answered) : shot_lag::FireEye::Count;
    const float tickS = float(Sight_TickPeriodUs(GameTitle::Halo4, 16667)) * 1.0e-6f;
    const uint64_t nowUs = Sight_NowUs();
    const float carryS = shot_lag::SampleCarrySeconds(sample.camera.stampUs, nowUs, tickS, kSightTransportMaxAgeUs);
    // The dev A/B (halo4_measured_fire_eye, a player build always 1): 0 sets a measured eye aside, so the shot carries
    // the controller root's eye one tick (the fallback model).
    const shot_lag::FireEye admitted =
        asked == shot_lag::FireEye::Measured && !g_config.halo4_measured_fire_eye ? shot_lag::FireEye::Unproven : asked;
    const shot_lag::FireEye verdict = shot_lag::ChooseSampleFireEye(
        admitted, measured, sample.camera.valid, sample.camera.eye, sample.camera.velValid ? sample.camera.vel : nullptr,
        carryS, eye);
#if HALOMCCVR_DEV_DIAGNOSTICS
    // Both ways, every fire the evaluator answered: its answer and the eye the fallback would carry, against E_P, with
    // the interpolator's (2 - factor) prediction. The factor read inside the tick is still the one E_P was drawn at.
    if (answered && sample.camera.valid)
    {
        float carried[3]{};
        const bool carriedValid = shot_lag::ChooseSampleFireEye(
            shot_lag::FireEye::Count, nullptr, true, sample.camera.eye,
            sample.camera.velValid ? sample.camera.vel : nullptr, carryS, carried) == shot_lag::FireEye::CarriedTick;
        int64_t tick = -1;
        float factor = NAN, tickMs = NAN;
        Halo4ClockRead(tick, factor, &tickMs);
        if (!std::isfinite(tickMs))
            tickMs = tickS * 1000.0f;
        auto& state = g_halo4FireEye;
        AcquireSRWLockExclusive(&state.lock);
        state.diag.fires.Add(sample.camera.eye, sample.camera.velValid ? sample.camera.vel : nullptr, answer,
                             carriedValid ? carried : nullptr, factor, tickMs);
        ReleaseSRWLockExclusive(&state.lock);
    }
    {
        auto& state = g_halo4FireEye;
        AcquireSRWLockExclusive(&state.lock);
        ++state.diag.verdicts[std::min(static_cast<int>(verdict), static_cast<int>(shot_lag::FireEye::Count))];
        ReleaseSRWLockExclusive(&state.lock);
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
const bool g_halo4FireEyeRegistered = (Sight_SetFireEyeSource(GameTitle::Halo4, &Halo4FireEyeForShot), true);

// ---- install / remove (with the Halo 4 core, after the muzzle lane; before it on teardown) -------------------------
void InstallHalo4FireEye(uintptr_t base, size_t size, uint32_t generation)
{
    auto& state = g_halo4FireEye;
    state.evaluator.store(nullptr, std::memory_order_release);
    AcquireSRWLockExclusive(&state.lock);
    state.proof = shot_lag::EvaluatorEyeProof{};
    state.reportedState = shot_lag::EyeProof::State::Unproven;
    state.reportedPath = false;
#if HALOMCCVR_DEV_DIAGNOSTICS
    state.diag = Halo4FireEyeDiag{};
#endif
    ReleaseSRWLockExclusive(&state.lock);
    state.generation.store(generation, std::memory_order_release);
#if HALOMCCVR_DEV_DIAGNOSTICS
    {
        const char* why = nullptr;
        if (!g_halo4Clock.Resolve(base, size, mcc_clock::kHalo4, &why))
            LOG("Halo 4 fire eye (dev): MCC's frame clock did not resolve (%s); the (2 - factor) prediction and the "
                "AS SEEN ticks are unread", why ? why : "?");
        else
            LOG("Halo 4 fire eye (dev): MCC's frame clock decoded from code -- TLS index +0x%X (the camera core's "
                "+0x%X), game_time TLS+0x%X (tick +0x%X, fraction +0x%X, seconds per tick +0x%X), interpolator "
                "TLS+0x%X (factor +0x%X)", g_halo4Clock.layout.tlsIndexRva, kHalo4EngineTlsIndexRva,
                g_halo4Clock.layout.gameTimeSlot, g_halo4Clock.layout.tickOffset,
                g_halo4Clock.layout.fractionOffset, g_halo4Clock.layout.secondsPerTickOffset,
                g_halo4Clock.layout.interpSlot, g_halo4Clock.layout.factorOffset);
    }
#endif
    // ADDR-HYGIENE: guarded -- the unit camera position's entry (unique, pinned) and the fire routine's own call of it.
    constexpr uint32_t kEvaluatorRva = 0x5F8A1C, kFireCallRva = 0x617FBF;
    constexpr char kEvaluatorSignature[] =
        "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC E0 00 00 00 44 8B 05 ?? ?? ?? ?? 33 FF";
    const uintptr_t match = sig::Find(base, size, kEvaluatorSignature);
    bool proven = match == base + kEvaluatorRva &&
                  !sig::Find(match + 1, base + size - match - 1, kEvaluatorSignature) && size > kFireCallRva + 5;
    if (proven)
    {
        const auto* call = reinterpret_cast<const uint8_t*>(base + kFireCallRva);
        int32_t rel = 0;
        memcpy(&rel, call + 1, sizeof(rel));
        proven = call[0] == 0xE8 && base + kFireCallRva + 5 + static_cast<intptr_t>(rel) == base + kEvaluatorRva;
    }
    if (!proven)
    {
        LOG("Halo 4 fire eye (flat parity) StockFallback: the unit camera position +0x%X did not prove (entry %s, the "
            "fire's call +0x%X); a muzzle shot keeps carrying the controller root's eye one tick", kEvaluatorRva,
            match ? (match == base + kEvaluatorRva ? "ambiguous or edge changed" : "moved") : "missing", kFireCallRva);
        return;
    }
    state.evaluator.store(reinterpret_cast<Halo4UnitCameraPositionFn>(base + kEvaluatorRva), std::memory_order_release);
    LOG("Halo 4 fire eye (flat parity) Installed: the unit camera position +0x%X (the fire's own call +0x%X) is asked "
        "inside each muzzle shot and, at rest, from the unit's own tick; used as the fire eye once proven equal to the "
        "engine eye at rest", kEvaluatorRva, kFireCallRva);
}

void RemoveHalo4FireEye()
{
    g_halo4FireEye.evaluator.store(nullptr, std::memory_order_release);
#if HALOMCCVR_DEV_DIAGNOSTICS
    g_halo4Clock.Reset();
#endif
}

// Cold worker (with ReportHalo4Muzzle): the dev HALO 4 FIRE EYE line.
void ReportHalo4FireEye()
{
    const uint32_t asks = g_halo4FireEye.asks.exchange(0, std::memory_order_relaxed);
    const uint32_t unanswered = g_halo4FireEye.unanswered.exchange(0, std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
    auto& state = g_halo4FireEye;
    AcquireSRWLockExclusive(&state.lock);
    const Halo4FireEyeDiag d = state.diag;
    state.diag = Halo4FireEyeDiag{};
    const shot_lag::EyeProof::State proofState = state.proof.proof.state;
    const uint32_t streak = state.proof.proof.agree;
    ReleaseSRWLockExclusive(&state.lock);
    const uint32_t samples = d.samples[0] + d.samples[1];
    uint32_t verdicts = 0;
    for (uint32_t v : d.verdicts)
        verdicts += v;
    if (!samples && !asks && !verdicts)
        return;
    char both[640]{};
    if (!d.fires.Format(both, sizeof(both)))
        std::snprintf(both, sizeof(both), "FIRES 0");
    char verdictText[256]{};
    int used = 0;
    for (int v = 0; v <= static_cast<int>(shot_lag::FireEye::Count); ++v)
        if (d.verdicts[v] && used >= 0 && used < static_cast<int>(sizeof(verdictText)))
            used += std::snprintf(verdictText + used, sizeof(verdictText) - used, "%s%s %u", used ? ", " : "",
                                  v < static_cast<int>(shot_lag::FireEye::Count)
                                      ? shot_lag::FireEyeName(static_cast<shot_lag::FireEye>(v)) : "?",
                                  d.verdicts[v]);
    LOG("HALO 4 FIRE EYE (dev): proof %s, streak now %u (best %u) of %u at rest within %.0f mm; evaluator %s; key "
        "halo4_measured_fire_eye=%d; samples %u (tick %u, shot ask %u) -- not counted: not our unit %u, seated %u, "
        "cinematic %u, no intent %u, another generation %u, no camera stamp %u, stale %u, eye velocity unknown %u, "
        "moving %u, evaluator failed %u; AT REST %u: off E_P avg %.0f mm (min %.0f, max %.0f), agree %u, between %u, "
        "disagree %u; %u asked in a fire (%u unanswered); fire-eye verdicts [%s]. %s",
        proofState == shot_lag::EyeProof::State::Proven ? "PROVEN"
        : proofState == shot_lag::EyeProof::State::Disproven ? "DISPROVEN" : "unproven",
        streak, d.bestStreak, shot_lag::EyeProof::kAgreeToProve, static_cast<double>(shot_lag::EyeProof::kAgreeMm),
        state.evaluator.load(std::memory_order_acquire) ? "resolved" : "NOT resolved",
        g_config.halo4_measured_fire_eye, samples, d.samples[0], d.samples[1], d.notOurs, d.seated, d.cinematic,
        d.noIntent, d.otherGeneration, d.noStamp, d.stale, d.velocityUnknown, d.moving, d.evaluatorFailed, d.restN,
        d.restN ? d.restSumMm / d.restN : 0.0, d.restMinMm, d.restMaxMm, d.restAgree, d.restNeither, d.restDisagree,
        asks, unanswered, verdictText, both);
#else
    (void)asks;
    (void)unanswered;
#endif
}
