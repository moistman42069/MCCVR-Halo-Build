#pragma once

// SIGHT LAYER -- the title-agnostic half of trace-and-reaim for every title other than Reach (Reach keeps
// its own lane, reach_sight_trace.inl, which this generalises). It is only plumbing between threads:
//
//   1. the aim path (the input poll, or whichever thread builds the controller aim for that title)
//      publishes the INTENT RAY: the rendered centre eye it starts at, the controller's direction, and
//      the ENGINE EYE the title aims its shots from -- all in the title's own world units;
//   2. the title's simulation-thread hook traces that ray with the title's own collision query and
//      publishes the range, which the layer also SMOOTHS for the aim: the bend goes as 1/range, so the
//      inverse range is averaged over ~120 ms (a sweep off a near edge would otherwise snap the aim in
//      one trace), and a miss counts as the trace's far end, as Reach's aim does (SightSmoothInverseRange);
//   3. the aim path bends its aim so the engine's line from the engine eye passes through the point at
//      that range (aim_solve::converge_direction), actuates it, and marks the aim in effect as converged;
//   4. the compositor draws the reticle on the eye-anchored ray at the traced depth while it is.
//
// A title that cannot trace simply never publishes a result, and its aim path keeps its old behaviour:
// the fallback runs only when this cannot resolve. The player key converged_aim = 0 turns the whole layer
// off (no ray, no trace, no convergence): every title then aims as it did before it.

#include "../common/runtime_types.h"
#include "../common/aim_solve_logic.h"
#include "../common/converge_policy.h"
#include "../common/seat_converge_logic.h"
#include "../common/shot_lag_logic.h"
#include "../common/sight_hit_class_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// The intent ray is traced 1 km out, like Reach's and Halo: Campaign Evolved's (a Blam world unit is 10 ft).
inline constexpr float kSightWorldUnitsPerMetre = 1.0f / 3.048f;
inline constexpr float kSightTraceLengthWu = 1000.0f * kSightWorldUnitsPerMetre;
inline constexpr float kSightCmPerWorldUnit = 304.8f;
// A trace result older than this is not used (the trace runs every ~30 ms): no fresh result means the
// simulation stalled or the trace is off, and the shot genuinely cannot be resolved.
inline constexpr uint64_t kSightFreshMs = 150;
inline constexpr uint64_t kSightTraceIntervalMs = 30;
// A published ray older than this is not traced (and asking is what keeps the trace running).
inline constexpr uint64_t kSightRayFreshMs = 100;
// Start 5 cm down the ray so the trace clears the head it starts in; ranges are still from the ray origin.
inline constexpr float kSightTraceSkipWu = 0.05f * kSightWorldUnitsPerMetre;
// The time constant of the inverse-range average the aim converges on (Reach's: reach_aim_provider.cpp).
inline constexpr float kSightRangeTauMs = 120.0f;

// One trace's step of the range the aim converges on, kept as the INVERSE range (the bend goes as 1/range)
// and averaged over kSightRangeTauMs, so a sweep off a near edge onto a far wall does not snap the aim in
// one trace. A MISS means the target is at least the whole trace away, so it feeds the far end: a sustained
// miss converges far, exactly as Reach's aim does (Game_ReachSightSample hands its provider the trace
// length on a miss). Do not hold the last range on a miss instead: the aim then stays converged on the last
// near hit (1.6 m, say) while the ray points at open sky, bending every far shot by the eye-to-hand parallax.
// `continuous` false (a new generation, or a gap in the traces) starts afresh from this sample.
inline float SightSmoothInverseRange(float invPerWu, bool continuous, bool hit, float rangeWu, float dtSeconds)
{
    if (!continuous)
        invPerWu = 0.0f;
    const float sampleWu = hit && std::isfinite(rangeWu) && rangeWu > 0.0f ? rangeWu : kSightTraceLengthWu;
    return aim_solve::ema_inverse_range(invPerWu, sampleWu,
        aim_solve::ema_alpha(kSightRangeTauMs, std::clamp(dtSeconds, 0.001f, 0.1f)));
}

// THE SIGHT CLOCK: every stamp and every age in this layer -- rays, traces, the converged and written
// marks, the intent point -- is QueryPerformanceCounter time, in milliseconds (Sight_NowMs) or microseconds
// (Sight_NowUs). GetTickCount64 ticks in 15.6 ms steps, which is half of the 25-40 ms lag the shot-lag witness
// measures. The ray publishers (Sight_PublishRay, the auxiliary rays) stamp `sampleMs` THEMSELVES at the publish,
// so no caller's clock can reach a reader; the trace driver's `nowMs` is Sight_NowMs() too. sight_layer.cpp reads no
// other clock (tools/check_sight_clock.py, a CTest guard).
uint64_t Sight_NowMs();
uint64_t Sight_NowUs();

struct SightRay
{
    uint32_t generation = 0;
    uint64_t sampleMs = 0;  // Sight_NowMs(), stamped by Sight_PublishRay (a caller's value is ignored)
    float head[3]{};        // the rendered centre eye: the intent ray starts here
    float dir[3]{};         // unit controller direction
    float eye[3]{};         // the engine eye the title aims its shots from
    int32_t unit = -1;      // the local unit the ray belongs to (the trace ignores it)
    int32_t ignoreB = -1;   // further objects the trace passes through (seated: the vehicle), or -1
    int32_t ignoreC = -1;
    // The engine eye's velocity (world units per second) over ~100 ms, estimated on the publishing thread from the
    // published eyes (shot_lag::EyeTrack, Sight_PublishRay fills it); eyeVelValid false = unknown, never 0.
    float eyeVel[3]{};
    bool eyeVelValid = false;
    // When `eye` was sampled (Sight_NowUs; 0 = at the publish). A title whose eye comes from a render passes that
    // render's stamp, so the velocity is measured on the eye's own clock.
    uint64_t eyeStampUs = 0;
};

struct SightTrace
{
    uint32_t generation = 0;
    uint64_t sampleMs = 0;
    bool hit = false;
    float rangeWu = 0.0f;         // this trace's own result; on a miss the trace length
    float convergeRangeWu = 0.0f; // the smoothed range the aim converges on (a miss counts as the far end); 0 = no trace yet
    // The range the aim was bent through: what a shot check must measure against.
    float AimRangeWu() const { return convergeRangeWu > 0.0f ? convergeRangeWu : rangeWu; }
};

// The player key converged_aim: false turns the layer off (no ray, no trace, no convergence).
bool Sight_AimEnabled();

void Sight_PublishRay(GameTitle title, const SightRay& ray);
bool Sight_ReadRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightRay& out);
// The simulation-thread trace driver's rate limit: true at most once per kSightTraceIntervalMs per title.
bool Sight_TraceDue(GameTitle title, uint64_t nowMs);
void Sight_PublishTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu, uint64_t costTicks);
bool Sight_ReadTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightTrace& out);
void Sight_NoteTraceFault(GameTitle title);
bool Sight_TraceFaulted(GameTitle title);
void Sight_ResetTitle(GameTitle title);   // a new title generation: forget faults and stale state

// THE TRACE'S STANCE (Halo CE and Halo 2 seats; 0 = on foot, a seat's seat_converge::StanceKey otherwise).
// A title whose ONE trace serves both the on-foot ray and a seat's says, on its trace thread and before each trace,
// which it is about to run. A change drops the published result and the smoothed range at once: a range measured on
// foot must never bend a seat's aim (nor a seat's the walk after it), and the 150 ms freshness window alone would
// let it for a few polls. A consumer that needs to be sure asks Sight_Stance and compares. Titles that never call
// this stay at 0.
void Sight_SetStance(GameTitle title, uint32_t stance);
uint32_t Sight_Stance(GameTitle title);

// The aim now in effect for `title` is a converged one. `unitsPerVrMetre` is the title's current VR world
// scale (world units per metre of VR space), which the compositor needs to put the reticle at the depth
// the traced surface appears at.
void Sight_MarkConverged(GameTitle title, float unitsPerVrMetre);
bool Sight_ConvergedLive(GameTitle title);
// ...and it is WRITTEN: this update the game's aim IS the converged aim (direct drive, Halo 2's aim write,
// CE's control packet), not a target the stick servo is still turning the aim toward at the game's turn
// rate. The traced reticle needs only the first; the zoom lens needs both (Sight_ScopeConvergedLive).
void Sight_MarkAimWritten(GameTitle title);
bool Sight_AimWrittenLive(GameTitle title);

// One-shot convergence for a title that aims with a direction: bend `dir` (unit, world) from `head`
// through the point at the smoothed traced range, as seen from `eye`. False (out untouched) when there is
// no fresh trace for this generation, no hit yet, or the geometry cannot resolve. A bent aim steeper than
// `maxPitchDeg` (the steepest pitch the caller's actuation can reach) is HELD AT THAT PITCH and still
// returned as converged: a close ceiling or floor is natural geometry, not our code failing.
// `outWhy`, when given, receives why it declined (our code's reasons only: converge::ConvergeOff).
// `railHold`, when given, is the caller's hysteresis for that clamp (aim_solve::RailHold: one per actuation loop,
// owned by the thread that calls; a call that does not clamp, including every decline, forgets it).
bool Sight_ConvergedDirection(GameTitle title, uint32_t generation, const float head[3], const float dir[3],
                              const float eye[3], float out[3], float maxPitchDeg = 90.0f,
                              float* outRangeWu = nullptr, bool* outHit = nullptr,
                              converge::ConvergeOff* outWhy = nullptr, aim_solve::RailHold* railHold = nullptr);

// For the compositor (any thread): the active title's traced range along its eye-anchored intent ray, in
// VR METRES, and whether it hit -- only while that title's aim in effect is converged. Never Reach
// (Game_ReachSightReticleRange owns Reach).
bool Game_SightReticleRange(float& outRangeM, bool& outHit);
// The world units per VR metre the title's converged aim last stamped (Sight_MarkConverged): the scale
// Game_SightReticleRange divided the traced range by. 0 before the first converged poll.
float Sight_UnitsPerVrMetre(GameTitle title);

// SCOPE CONVERGENCE: a zoom lens at the RENDERED eye looks along the intent ray, which from that eye passes
// through the point the converged aim is bent through -- so the lens centre is the reticle's point, where
// looking along the game's aim (bent to leave from the engine eye) would run parallel to the shot line and
// off it by the eye's distance from the character. Only while the player key scope_converge is on and the
// converged aim is in effect AND written (Sight_ConvergedLive, Sight_AimWrittenLive): under the stick servo
// the shot follows a game aim still turning toward the target, and the lens keeps showing that aim.
// A DIRECTION, never a point: the lens origin is this frame's eye while the ray's head was sampled on another
// thread and frame, so a look-at point would swing the lens by the head's motion since that sample over the
// range, magnified by the zoom. Sight_ScopeConvergedDirection hands back the ray the aim path published
// (Halo 3, ODST, Halo 4); a title that builds its own ray from the lens origin asks Sight_ScopeConvergedLive
// (Halo 2). False: the lens keeps its own direction, as before. Any thread.
bool Sight_ScopeConvergedLive(GameTitle title);
bool Sight_ScopeConvergedDirection(GameTitle title, float outDir[3]);

// SHOTS FROM THE MUZZLE (the player key shots_from_muzzle; aim_solve::muzzle_shot_direction). Each title's
// barrel-shot transaction already moves a shot's origin to the visible muzzle and runs it through the
// title's own obstruction check (that is the barrel aim, gun_barrel_aim, which also takes the barrel's
// direction). With this key instead, the shot keeps that origin but flies through the CONVERGED POINT --
// the intent ray's point at the range the converged aim is bent through -- so it lands where the shot from
// the engine eye would have, and the tracer and projectile come out of the gun.
// Sight_MuzzleShotsConverge: the key is on and the barrel aim (which wins) is off.
// Sight_AimMuzzleShot: after the title's obstruction check moved the muzzle from `before` to `position`,
// replace `direction` and `up` and return true; false leaves all three untouched, and the title then
// refuses the barrel shot, so it leaves the eye exactly as before -- when the aim in effect is not
// converged, the ray or trace is stale, or the geometry refuses (aim_solve::MuzzleShot, counted by reason; no
// distance-from-the-eye limit: the muzzle's identity is each title's weapon_muzzle receipt). The intent ray and engine eye are
// the published ray's (Halo 3, ODST, Halo 4), the caller's own (...FromRay: Halo 2, CE), or the caller's
// converged point itself (...Through: Reach, whose aim lane is its own). Any thread; counted per title.
// ...FromRay's pointDistance, when given, receives how far that point lies from the muzzle (world units) on
// success: a title whose firing assist aims at a point along the shot (Halo 2) aims at that one, from the
// same trace sample the direction was built from.
// THE CAMERA SAMPLE a shot site built its point from (flat parity, below): that sample's ENGINE eye E_P, when it was
// sampled (Sight_NowUs), and E_P's velocity (wu/s). Halo 2: the observer publication; CE: the gameplay context.
struct SightCameraSample
{
    bool valid = false;
    float eye[3]{};
    uint64_t stampUs = 0;
    bool velValid = false;
    float vel[3]{};
};
// False while coop_safe_shots is on: every site that would change the simulation on this machine only leaves it
// as the game built it -- a shot changed inside the fire routine (the muzzle shot, a re-aim through the intent
// point, Reach's hand ray, seat redirect and passenger origin, ODST's passenger origin, Halo 2's seated
// fire-from-eye), in Halo 2 the unit's aim vectors and the aim-assist calculation, contact melee's own
// damage, the first-person seat camera's answers to the simulation (native_vehicle_first_person.h) and the
// edit of a seat's loaded flags that ODST's and Reach's first-person seats are made with.
// The aim written into the control record and a pressed binding are shared input and are not
// affected. A site that stands a local shot down for this reason
// counts it (Sight_Report logs "CO-OP SAFE SHOTS" every 2 s).
// coop_safe_shots = 2 (automatic) asks the session the title's simulation thread last noted (Sight_NoteSession):
// true only while the game says the player is alone or the session is host-authoritative, on a settled run of
// fresh notes (session_read_logic.h: Classify, GateOpen). Any thread; a few atomic loads.
bool Sight_FireTimeChangesAllowed();
void Sight_NoteFireTimeStoodDown(GameTitle title);
// The session in progress as the game holds it, read by the title's simulation thread
// (src/common/session_read_logic.h): its simulation type, the players in the game and the machines other than
// this one (-1 each: not read; a type of -1: no game in progress). Sight_Report logs them whenever one changes
// ("SESSION <game>: ..."), so a solo level, a co-op host, a co-op joiner and a host-authoritative game can be told
// apart in a log. Each note also steps the automatic switch's gate (Sight_FireTimeChangesAllowed), which acts on
// it only under coop_safe_shots = 2.
void Sight_NoteSession(GameTitle title, int type, int players, int otherMachines);
// The simulation type last noted for the title (-1: none). Any thread.
int Sight_SessionType(GameTitle title);
// THE WRITTEN AIM, LED (the player key aim_lead_ticks; src/common/aim_lead_logic.h). A record written at one control
// pass is first used by the next simulation tick, so a shot leaves along a hand sample about a tick old. The title's
// simulation-thread write of the ON-FOOT record hands its setpoint through here just before it writes: `yaw` and
// `pitch` come back carried ahead by the setpoint's own rate times aim_lead_ticks ticks of `tickUs` (0: unmeasured,
// 60 Hz) -- or unchanged: the key off, an aim at rest, a jump (a snap turn, a recentre), a gap, a stop, or
// `drawnFromRecord`. That last is the caller's word that something drawn this frame looks along the game's aim (the
// zoom lens while it is not on the converged ray): nothing drawn may move with the lead, so the record is not led
// then. `sampleKey` names the tracking frame the setpoint was solved from (0: unknown), so a pass that meets one
// frame twice, or misses one, does not read as a change of speed. The caller still wraps the yaw and clamps the pitch.
// The write is shared input, not a fire-time change: it is the same in solo and co-op, and coop_safe_shots does not
// stand it down. One writer thread per title; nothing here allocates, logs or blocks. Sight_Report logs "AIM LEAD"
// every 2 s: how many writes were led and by how much, and why the others were not.
void Sight_LeadWrittenAim(GameTitle title, uint64_t sampleKey, uint32_t tickUs, bool drawnFromRecord, float& yaw,
                          float& pitch);
// The title's update did not end in an on-foot write (a seat, nothing written, disarmed): the next on-foot write
// starts afresh. The same thread as Sight_LeadWrittenAim.
void Sight_ForgetAimLead(GameTitle title);
// The key is on (any thread): a caller need not gather `drawnFromRecord` otherwise.
bool Sight_AimLeadOn();
// The three settings that change a shot inside the fire routine, as they are IN EFFECT: each reads off while
// fire-time changes are not allowed. Everything but the settings menu and the dev pad reads these, never the raw
// fields, so the one predicate above governs every lane.
bool Sight_ShotsFromMuzzle();
bool Sight_GunBarrelAim();
bool Sight_IndependentDualAim();
// Contact melee (physical_melee) as it is IN EFFECT, on the same predicate: its strike applies damage through the
// game's melee routine on this machine only. While it reads off, the gesture lane presses the game's melee
// binding for the same swing (game.cpp), so the setting still gives a melee in every game that lane serves
// (all but Halo CE).
bool Sight_PhysicalMelee();
bool Sight_MuzzleShotsConverge();
bool Sight_AimMuzzleShot(GameTitle title, uint32_t generation, const float before[3], const float position[3],
                         float direction[3], float up[3]);
// `camera` (Halo 2, CE: the sample head/dir/eye came from): the point is moved with the eye to the fire (flat parity,
// Sight_ShotPointSample) when the keys want it; null keeps the world point. `pointDistance` is from the point used.
bool Sight_AimMuzzleShotFromRay(GameTitle title, uint32_t generation, const float head[3], const float dir[3],
                                const float eye[3], const float before[3], const float position[3],
                                float direction[3], float up[3], float* pointDistance = nullptr,
                                const SightCameraSample* camera = nullptr);
bool Sight_AimMuzzleShotThrough(GameTitle title, const float point[3], const float rayDir[3],
                                const float before[3], const float position[3], float direction[3], float up[3]);
// A title that could not even ask (no converged point of its own): counted with the rest.
void Sight_NoteMuzzleShotUnconverged(GameTitle title);
// A seated redirect refused DEFENSIVELY because the gun points elsewhere, naturally (a seat held at its stop,
// a rate-capped turret): the shot leaves as the game fires it. The convergence claim is not touched.
void Sight_NoteMuzzleShotHeld(GameTitle title);

// THE INTENT POINT (shot_lag_logic.h). The aim path publishes its converged intent as a WORLD POINT P,
// not only as the angles it solved -- angles from a render-time eye land a frame or more later as a line parallel to
// the one meant, moved by the eye's travel in between (Reach: mean 93 mm, p90 187, max 307). A shot site that knows
// its own origin aims through P from there (Sight_ReaimThroughPoint). `point` comes from ONE frame-coherent
// hand/camera pair where the title has one (Halo 3, ODST: the render that used the hand's head sample); `solvePoint`
// is the point the published angles were bent through (the same unless the pair was older). Halo 3, ODST and Halo 4
// publish from Game_ComputeAimStick; Reach keeps its own lane (ReachSightReadActuated / ReachSightReadIntentSample,
// whose camera sample is stamped at the local shot ray's publish). Any thread reads.
struct SightIntent
{
    shot_lag::IntentPoint p{};
    float solvePoint[3]{};
};
void Sight_PublishIntent(GameTitle title, const SightIntent& intent);
bool Sight_ReadIntent(GameTitle title, SightIntent& out);
// The intent's verdict for a shot site now: fresh within maxAgeMs, this generation, the converged aim live, no
// natural hold, the solve not held at the pitch rail. `out` is filled whenever an intent was read.
shot_lag::IntentUse Sight_IntentForShot(GameTitle title, uint64_t maxAgeMs, bool naturalHeld, SightIntent& out);
inline constexpr uint64_t kSightIntentShotMaxAgeMs = 150;

// THE SHOT POINT MOVED WITH THE EYE -- FLAT PARITY (every build; the dev key shot_point_transport,
// default 1 and fixed at 1 in a player build, is the A/B; shot_lag_logic.h 4a). Stock flat Halo fires through its
// crosshair from the game's eye AFTER the fire tick's move: one tick of strafe ahead of the crosshair the player aimed
// with, so the impact lands on the crosshair of the frame that shows it (the flat probe's AS SEEN measure). P is a
// world point fixed at a camera sample a tick or so older. So a local shot aims through P' = P + (E_fire - E_P): E_P
// the engine eye of P's camera sample, E_fire the game's eye at the fire -- MEASURED where the title has one (Halo 3
// on foot, Sight_SetFireEyeSource: the query's own start, or the evaluator asked at a muzzle shot, else this tick's
// eye carried a tick), else EXTRAPOLATED as E_P + v * age (ODST). Covered: Halo 3 and ODST -- the
// crosshair-assist query re-aim (LegacySightReaimCrosshairQuery) and the muzzle shot (Sight_AimMuzzleShot), on foot;
// seats under the second dev key shot_point_transport_seated (default 1); and Halo 3's on-foot record
// recomposition (shot_lag::RecomposeRecord). Also covered: the other titles'
// on-foot shots that aim through a WORLD point: Halo 2's and CE's muzzle shots (E_fire their camera evaluator asked at
// the fire once proven, shot_lag::EvaluatorEyeProof, else E_P carried a tick: Sight_ShotPointSample), CE's player
// assist ray, and Halo 4's muzzle shot (its unit camera position asked in the fire once proven, else
// E_P carried a tick: halo4_fire_eye.inl -- Halo 4 interpolates its camera, so a carried tick alone is short). A
// consumer that applies a DIRECTION solved from E_P at the fire's own eye (the unit aim Halo 2, CE and Halo 4 write,
// and every record) already IS flat parity: its line from E_fire passes through P + (E_fire - E_P). REACH: its on-foot
// shots aim through a world point -- the unit-adjust and
// crosshair-query re-aims and the muzzle shot, all through the provider's actuated point -- so by the rule they trail a
// strafe by E_fire - E_P unless Reach fires from the render's eye (its native shot line measures 93 mm off the
// solve's eye while moving). Reach is covered MEASURED OR NOTHING
// (shot_lag_logic.h 4f): its intent carries the camera sample's stamp (the local shot ray's publish), and E_fire is
// Reach's own unit camera-position evaluator asked inside the fire for our unit (reach_fire_eye.inl), used only once
// proven equal to E_P at rest; never carried or extrapolated (SightPointSample::measuredOnly), so an unproven or absent
// evaluator leaves the shot on P, counted. Reach's record (the direct drive's angles, solved from E_P) is a direction
// and is not shifted; its seats are not covered at all. The reticle is never moved: it stays on P.
enum class SightTransportSite : uint8_t
{
    CrosshairAssist = 0,   // the crosshair-assist query re-aim (Halo 3, ODST, Reach); CE's player assist ray
    Muzzle,                // the muzzle shot (Halo 3, ODST, Halo 2, CE, Halo 4, Reach)
    Record,                // Halo 3's on-foot record recomposition
    UnitAdjust,            // Reach's unit-adjust re-aim (the shot's own origin and direction)
    Count
};
// A point a shot site aims through, with the camera sample it was built from (Sight_ShotPointSample).
struct SightPointSample
{
    float point[3]{};          // P
    float dir[3]{};            // the intent ray's direction (only splits the witness's shift along travel)
    SightCameraSample camera;  // E_P, its stamp and velocity
    // Reach: P moves only with a MEASURED fire eye. Without one the shot keeps P (Transport::NoFireEye) -- E_P's
    // velocity is never used to extrapolate one; it still splits the witness's shift along travel.
    bool measuredOnly = false;
};
// A title's eye at the fire tick (Halo 3 on foot: game.cpp Halo3FireEyeForShot; Halo 2 and CE: their camera evaluator
// asked at the fire, else `sample`'s eye carried a tick), on the fire thread. `siteEye`: the eye the shot site itself
// already holds (the crosshair query's own start), else null; `askEvaluator`: the site (a muzzle shot) may have the
// title's evaluator asked now. `sample`: the camera sample P was built from. Returns its verdict; when usable
// (shot_lag::FireEyeUsable) `eye` is E_fire and `ageMs` its age now.
using SightFireEyeFn = shot_lag::FireEye (*)(const float* siteEye, bool askEvaluator, const SightPointSample& sample,
                                             float eye[3], float& ageMs);
void Sight_SetFireEyeSource(GameTitle title, SightFireEyeFn source);
// The keys' verdict for a shot of this kind: a covered title (Halo 3, ODST, Halo 2, CE, Halo 4, Reach on foot),
// shot_point_transport on, and seated shots only with shot_point_transport_seated as well (never Reach's seats). A
// player build: on foot yes, seated yes.
bool Sight_TransportWanted(GameTitle title, bool seated);
// A title's simulation tick, from a per-tick hook (every build; several units per tick are one tick), and its
// measured period (us), else `fallbackUs` while unmeasured. Also feeds the dev SHOT LAG line's tick.
void Sight_NoteTitleTick(GameTitle title);
uint32_t Sight_TickPeriodUs(GameTitle title, uint32_t fallbackUs);
// P' for this intent's point -- seated the solve's point (solvePoint) from the solve's eye, on foot P from its camera
// sample -- WITHOUT consulting the keys (the impact witness asks it for every fire). `siteEye` / `askEvaluator`: as
// SightFireEyeFn (on foot only). `out`/`shift` written only when applied; `fire` (optional) receives the fire-eye
// verdict, `fireAgeMs` / `pointAgeMs` (optional, -1 unknown) the fire eye's and the camera sample's ages. The fire
// thread (a fire-eye source may evaluate the title's eye).
shot_lag::Transport Sight_TransportIntent(GameTitle title, const SightIntent& intent, bool seated,
                                          const float* siteEye, bool askEvaluator, float out[3], float shift[3],
                                          shot_lag::FireEye* fire = nullptr, float* fireAgeMs = nullptr,
                                          float* pointAgeMs = nullptr);
// A TITLE'S FIRE EYE IS MEASURED BY ASKING THE GAME: its own camera evaluator, inside the simulation tick -- inside the
// native fire, and at rest for the answer's proof. True only where that may be asked: the keys want the transport on
// foot (Sight_TransportWanted) AND one-machine changes are allowed (Sight_FireTimeChangesAllowed). In a simulation
// another machine shares the shot is left as the game built it, so the eye would be measured for nothing; and the
// ask is a call of the game's own that only this machine makes inside a tick, which is not without effect there
// (docs/COOP-SHARED-SIMULATION-2026-10-13.md: a saved Reach co-op film replayed with and without it). The written
// aim's own use of the keys (a title's record, composed between ticks) does not come through here.
bool Sight_FireEyeAsked(GameTitle title);
// The point a local shot at `site` aims through: `reference` itself, or -- the keys on (Sight_TransportWanted) and
// one-machine changes allowed (every use of the point is a change to the shot inside its fire) -- P' in `storage`
// when the transport applies. `siteEye`: the eye the site holds (the crosshair query's start); a muzzle shot has
// none and asks for the title's eye. Every outcome under the keys is counted (the dev SHOT LAG transport line).
const float* Sight_ShotPoint(GameTitle title, SightTransportSite site, const SightIntent& intent, bool seated,
                             const float* reference, float storage[3], const float* siteEye = nullptr);
// The same for a site that built its point itself (Halo 2, CE: no published intent): `sample` is P and its camera.
shot_lag::Transport Sight_TransportSample(GameTitle title, const SightPointSample& sample, bool seated,
                                          const float* siteEye, bool askEvaluator, float out[3], float shift[3],
                                          shot_lag::FireEye* fire = nullptr, float* fireAgeMs = nullptr,
                                          float* pointAgeMs = nullptr);
const float* Sight_ShotPointSample(GameTitle title, SightTransportSite site, const SightPointSample& sample,
                                   bool seated, const float* reference, float storage[3],
                                   const float* siteEye = nullptr);
// The calling thread's last Sight_ShotPoint decision under the keys (valid = transported): P' and P' - P, which site,
// how, the fire eye's verdict and when (Sight_NowUs). The dev impact witness reads it on the fire thread.
struct SightLastShotPoint
{
    bool valid = false;
    SightTransportSite site = SightTransportSite::Count;
    shot_lag::Transport how = shot_lag::Transport::Count;
    shot_lag::FireEye fire = shot_lag::FireEye::Count;
    uint64_t stampUs = 0;
    float point[3]{}, shift[3]{};
};
bool Sight_LastShotPoint(SightLastShotPoint& out);
// A consumer that transports by itself (the record recomposition): count it like Sight_ShotPoint does.
void Sight_NoteTransport(GameTitle title, SightTransportSite site, shot_lag::Transport how, shot_lag::FireEye fire,
                         const float* shift, const float* dir, const float* vel, float fireAgeMs, float pointAgeMs);
// The oldest camera sample an extrapolated transport carries (identity / freshness, not a size limit).
inline constexpr uint64_t kSightTransportMaxAgeUs = 250000;

// SHOT RE-AIM SITES (every build): which of a title's shot sites re-aimed a query or shot through the intent point.
enum class SightShotSite : uint8_t
{
    CrosshairAssist = 0,   // the title's own crosshair / firing-assist collision query
    UnitAdjust,            // Reach's unit-adjust (the shot's own origin and direction)
    Muzzle,                // shots from the muzzle (Sight_AimMuzzleShot)
    Count
};
// Re-aims `vector` (from `start`) through `point`: true and `vector` replaced when it applied; false leaves it as the
// game built it. Counted per title and site with the reason (Sight_Report logs "SHOT REAIM" every 2 s). `seated`: a
// seat's query, counted apart as well -- with the seat's record chosen upstream of replication (Halo 3,
// seat_upstream_logic.h) the game's own line should already pass through P, so a seated re-aim that still acts (and
// its bend) is the MULTIPLAYER RESIDUAL: what a host firing from the record alone would miss by.
bool Sight_ReaimThroughPoint(GameTitle title, SightShotSite site, const float start[3], float vector[3],
                             const float point[3], bool seated = false);
// A shot site that did not try (the intent was not usable): counted with its reason.
void Sight_NoteReaimSkipped(GameTitle title, SightShotSite site, shot_lag::IntentUse why);

// SHOT LAG WITNESS (dev; empty in player builds). Per shot, after the native call decided its final ray: the miss of
// the shot line from the point the player meant, split along / across the player's travel, with the eye's speed and
// the stale-eye term (shot origin minus the eye the solve used). One window per title and PATH (how the shot was
// aimed), logged by the cold worker every 2 s:
//   SHOT LAG <title> [<path>; <mode>] (dev): N shots; speed 0-1 m/s: n, miss X mm (along a, across c); 1-4: ...;
//     4-10: ...; 10+: ...; mean lag axis along travel B mm; stale eye along travel S mm (implied dt T ms);
//     at range R m; seated=0/1 seat=<id>
// `mode`: the fire's own `predicted` flag where the title passes one (Halo 3 / ODST distributed simulation predicts
// local shots outside the campaign), and the measured simulation tick. Read-only: nothing decides on it.
enum class SightShotPath : uint8_t
{
    Native = 0,        // the game's own aim, as the record put it
    CrosshairAssist,   // the crosshair / firing-assist query (after any re-aim)
    Muzzle,            // shots from the muzzle
    UnitAdjust,        // Reach's unit-adjust, after any re-aim
    Count
};
// `reference` = P (world units); `solveEye` = the eye the solve used (may be null); `eyeVel` (wu/s, may be null).
// `predicted`: -1 unknown, else the fire's flag.
void Sight_NoteShotLag(GameTitle title, SightShotPath path, const float origin[3], const float direction[3],
                       const float reference[3], const float* solveEye, const float* eyeVel, int predicted,
                       bool seated, int32_t seat);
// The same, with the reference, eye and velocity from this title's published intent point (else its ray and trace).
void Sight_NoteShotLagFromIntent(GameTitle title, SightShotPath path, const float origin[3], const float direction[3],
                                 int predicted, bool seated, int32_t seat);
// The same for a title that publishes no intent (Halo 2, CE): the point is the caller's own intent ray (head, dir) at
// the range the aim converged on, the eye its own; the velocity is measured from these eyes by the witness itself.
void Sight_NoteShotLagFromRay(GameTitle title, SightShotPath path, const float origin[3], const float direction[3],
                              const float head[3], const float rayDir[3], const float eye[3], bool seated);
// A simulation tick of `title` seen by a per-tick hook (dev): the witness reports the measured tick period.
void Sight_NoteSimTick(GameTitle title);
// How the aim path built this title's intent point (dev): 0 this poll's hand with the render of its own frame, 1 an
// earlier hand with the render of its frame (this poll's frame not rendered yet), 2 no kept hand has a rendered
// frame (the solve's own pair), 3 the title publishes no camera frames (Halo 4).
void Sight_NoteIntentPairing(GameTitle title, int kind);

// Cold worker: one status line per title that is tracing, every 2 s.
void Sight_Report();

// WHAT THE INTENT RAY CAN HIT (sight_hit_class_logic.h). A trace whose flags leave out bipeds or
// vehicles converges the aim on the wall behind the target, and nothing else in the log would say so.
// Sight_AnnounceTraceQuery (every build; cold path): the title's trace driver says, once per install, when its
// flags are settled, which query it runs and with which flags; the line decodes them into structure / bipeds / vehicles /
// other objects with that title's own bit layout, and WARNS when a class is missing:
//   SIGHT <title> intent ray: <query>, flags 0x.. (<source>): stops on structure yes, bipeds yes, vehicles yes,
//     other objects yes (every one of N object types)[; the engine's own culls in these flags: ...]
// `seatedNote`: what the trace does in a seat (its ignore set), or that it does not trace there.
// Sight_NoteTraceHit / Sight_NoteLookPast (dev; empty in player builds): each trace's result by class, and each
// part of our own vehicle a seated trace looked past. Logged by Sight_Report every 2 s per title:
//   SIGHT HITS <title> (dev): N traces (S seated): structure a [x-y m], biped b [x-y m], vehicle c [x-y m],
//     other object d [x-y m, last type T], unknown-type object u, miss m; looked past our own vehicle: k (...)
// To check it in game: aim at an enemy or a parked vehicle in front of a far wall and the biped /
// vehicle count rises with the near range. `objectType`: the title's own object type (0 biped, 1 vehicle), -1
// unknown. One writer per title (its trace thread).
void Sight_AnnounceTraceQuery(GameTitle title, sight_hit::Engine engine, uint64_t flags, const char* query,
                              const char* source, const char* seatedNote);
void Sight_NoteTraceHit(GameTitle title, sight_hit::HitClass hit, int objectType, float rangeWu, bool seated);
void Sight_NoteLookPast(GameTitle title, sight_hit::HitClass part);
// The class of an object hit, from the title's own object type (-1 unknown).
inline sight_hit::HitClass Sight_ObjectHitClass(int objectType)
{
    return sight_hit::ClassifyHit(true, true, objectType);
}

// SEAT CONVERGENCE WITNESS, Halo CE and Halo 2 (seat_converge_logic.h). Each seated aim update says, for
// the seat it is in (`seatKey` = seat_converge::StanceKey, `role` its role), whether the aim it handed the game was
// the converged one, else the first reason it was not -- always ours, never a natural limit. Logged by Sight_Report
// every 2 s in EVERY build (a refusal nobody can see hides for a whole stint):
//   SIGHT <title> seat <key> (<role>): N aim updates, M converged; not converged: <reason>=n, ...
// Sight_NoteSeatShot (dev; empty in player builds): at each local seated fire, how far the traced point sat off a
// line the ENGINE built before the mod re-aimed anything -- `Barrel`: the shot as the fire routine took it (the
// weapon's barrel marker for a vehicle gun, the unit's aim for a passenger's own weapon), which is also the centre of
// the cone the engine pins a seated shot to; `Crosshair`: the engine's own deterministic camera line (what an
// unmodded host traces along). Logged with the line above:
//   SIGHT <title> seat shots (dev): barrel/unit line N: mean X mm / Y deg (max ...) at R m; engine crosshair line
//     N: mean ... (before the fire's re-aim; the re-aim itself is on the SHOT REAIM line)
enum class SightSeatLine : uint8_t { Barrel = 0, Crosshair, Count };
void Sight_NoteSeatPoll(GameTitle title, uint32_t seatKey, const seat_converge::Role& role, seat_converge::Why why);
void Sight_NoteSeatShot(GameTitle title, SightSeatLine line, const float origin[3], const float direction[3],
                        const float point[3]);
// The last seated aim update of `title` handed the game a converged aim for seat `seatKey` within `maxAgeMs` (the
// fire-time backstop re-aims only then).
bool Sight_SeatConvergedLive(GameTitle title, uint32_t seatKey, uint64_t maxAgeMs);

// AN AUXILIARY RAY: one the title's simulation-thread trace runs beside the intent ray, in the same tick, with the
// same native query, flags and ignores, into a slot of its own -- from a shot origin along a direction the game
// really aims, to the first surface. Nothing the aim converges on reads it: the intent ray, its trace, the smoothed
// range, the scope, the shots and the writes are untouched.
struct SightAuxRay
{
    uint32_t generation = 0;
    uint64_t sampleMs = 0;   // Sight_NowMs(), stamped by the publish (a caller's value is ignored)
    float origin[3]{};   // the game's shot origin (the engine eye on foot; the seat's eye in a seat)
    float dir[3]{};      // unit, the direction the game aims along
};

// SEAT LIMIT RAY (all builds; src/common/seat_limit_reticle_logic.h). While a seat's reticle is clamped at a natural
// angular stop (a passenger's cone, a turret's or a tank gun's pitch stop), the aim path publishes the ray the gun
// really aims along -- the seat's engine eye along the limit-clamped direction -- and the title's trace runs it as an
// auxiliary ray, so the reticle sits on the surface the gun's shots meet. Only while a seat is clamped: nothing is
// traced otherwise. rangeWu: from the ray's origin; on a miss the caller passes 0 and a read reports the far end.
void Sight_PublishLimitRay(GameTitle title, const SightAuxRay& ray);
bool Sight_ReadLimitRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightAuxRay& out);
void Sight_PublishLimitTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu);
bool Sight_ReadLimitTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu);
// VEHICLE CAMERA RAY (all builds; vehicle_cameras.inl). While a chase camera of the vehicle cameras is up with
// "collide" set, the camera thread publishes the ray from the point the camera is measured from toward where it
// sits, and the title's trace runs it as an auxiliary ray beside the intent ray, with the same ignores (the
// player's own vehicle and its parts), so the camera can be held short of a wall. Halo 3 for now.
void Sight_PublishCameraRay(GameTitle title, const SightAuxRay& ray);
bool Sight_ReadCameraRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightAuxRay& out);
void Sight_PublishCameraTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu);
bool Sight_ReadCameraTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu);
// VEHICLE AIM RING RAY (all builds; vehicle_cameras.inl, vehicle_aim_marker_logic.h). While the ring where the
// vehicle aims is shown, the camera thread publishes the line it marks -- from the game's own camera along the
// vehicle's heading at the game's aim pitch -- and the title's trace runs it as an auxiliary ray beside the intent
// ray, with the same ignores, for the depth the ring is drawn at. Halo 3 for now. rangeWu: from the ray's origin; on
// a miss the caller passes 0 and a read reports the far end.
void Sight_PublishMarkerRay(GameTitle title, const SightAuxRay& ray);
bool Sight_ReadMarkerRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightAuxRay& out);
void Sight_PublishMarkerTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu);
bool Sight_ReadMarkerTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu);
// The seat's reticle is clamped at a natural stop (stamped by each aim poll that clamps it; live for 150 ms). While
// it holds, a muzzle redirect is refused (the shot leaves along the game's clamped aim, through the clamped mark,
// never past the stop) and the zoom lens keeps the game's aim instead of the hand's ray. Any thread.
void Sight_MarkSeatLimitClamped(GameTitle title);
bool Sight_SeatLimitClampedLive(GameTitle title);
// Dev witness (empty in player builds): each clamped poll's edges and overshoot, and every 2 s per title
//   SEAT LIMIT <title>: reticle clamped N polls (yaw edge Y, pitch edge P, gun band G), max overshoot X deg
// `gunBand`: a look-steered driver's shot held at its guns' elevation band (Halo 3; halo3_vehicle_gun_band.inl).
void Sight_NoteSeatLimit(GameTitle title, bool yawEdge, bool pitchEdge, float overshootDeg, bool gunBand = false);

#if HALOMCCVR_DEV_DIAGNOSTICS
// [assess-only begin]
// DEV ASSESSMENT RAY (honest_reticle_assess; dev builds only, src/common/assess_reticle_logic.h). The reticle the
// assessment draws is the point the game's shot really reaches: from the game's own shot origin along the game's
// own aim, to the first surface. The aim path publishes that ray here (only while the key draws this title); the
// title's simulation-thread trace traces it as an auxiliary ray (above) into slots of its OWN: it never shares a
// slot or a latch with the seat limit ray, and a player build does not contain it.
using SightAssessRay = SightAuxRay;
void Sight_PublishAssessRay(GameTitle title, const SightAssessRay& ray);
bool Sight_ReadAssessRay(GameTitle title, uint32_t generation, uint64_t maxAgeMs, SightAssessRay& out);
// rangeWu: from the ray's origin; on a miss the caller passes 0 and a read reports the trace's far end.
void Sight_PublishAssessTrace(GameTitle title, uint32_t generation, bool hit, float rangeWu);
bool Sight_ReadAssessTrace(GameTitle title, uint32_t generation, uint64_t maxAgeMs, bool& hit, float& rangeWu);
// The assessment trace's OWN fault latch. A native fault in the assessment's trace switches off only the assessment
// trace for that title, until the title reinstalls (Sight_ResetTitle, or Reach's install); the title's intent trace
// (Sight_NoteTraceFault / its own faulted flag) belongs to the aim and is never touched by the assessment.
void Sight_NoteAssessTraceFault(GameTitle title);
bool Sight_AssessTraceFaulted(GameTitle title);
void Sight_ResetAssessTrace(GameTitle title);
// [assess-only end]
#endif

// CONVERGE-OFF WITNESS (dev; empty stubs in player builds, converge_policy.h). Natural in-game limits never
// switch the converged aim off; these lines are the evidence that they do not.
// Sight_NoteConvergeState: each aim poll in gameplay (Halo 3, ODST and Halo 4 from Game_ComputeAimStick,
// Reach from its provider) says whether the converged aim is in effect and, if not, the FIRST reason -- which
// is always our code's or a state convergence does not apply to.
// Sight_NoteNatural: the natural causes (which must never switch it off), counted only -- no decision reads them.
// Sight_NaturalHeld: one of those held this title's aim off the point within `windowMs` (the dev engine-trace
// witnesses skip such polls, so their miss statistics measure our solve, not a seat's stop).
// Sight_ReportConverge (cold worker, every loop): an edge line when a title's converged aim goes off or comes
// back (rate-limited), and every 2 s per title
//   SIGHT <title> converge-off <off>/<polls> polls: <Reason>=<n>, ...
//   SIGHT <title> natural (no switch-off) in <n> polls: latchHeld=.. outcomeMiss=.. stall=.. turret=.. ...
void Sight_NoteConvergeState(GameTitle title, bool live, converge::ConvergeOff why);
void Sight_NoteNatural(GameTitle title, const converge::NaturalEvents& events);
bool Sight_NaturalHeld(GameTitle title, uint64_t windowMs);
void Sight_ReportConverge();

// SHOT WITNESS (dev; compiled out of player builds): a title's firing path reports each local shot's ray --
// origin and direction in world units, as the engine adjusted them -- and the cold worker logs, per title
// every 2 s, where those shots pass the surface under the reticle (the intent ray's latest trace) and the
// converged point, in the intent ray's own frame (up / right, mm), and where they left from relative to the
// engine eye and the rendered eye. "The shots land below the reticle" is then a number, and so is its cause.
void Sight_NoteShot(GameTitle title, uint32_t generation, const float origin[3], const float direction[3]);
void Sight_ReportShots();

// ENGINE-RAY CENSUS (dev; the callers gate it with the dev key sight_ray_census): which of the title's own
// collision queries is its crosshair / shot query? A title's collision hook reports each query the game
// makes with its caller's RVA. One that starts within 0.3 m of the solve's engine eye and runs within 2 deg
// of the eye -> traced-point direction counts as ALIGNED, with how far the traced point sits off its line.
// Needs the title's ray and trace published. Compare windows with and without shots: a caller that
// appears only on shots is the shot's own query. Compiled out of player builds (empty stubs).
void Sight_NoteEngineRay(GameTitle title, uint32_t generation, uint32_t callerRva, uint32_t profile,
                         uint64_t flags, const float start[3], const float end[3]);
// Cold worker, every 5 s: the census lines of every title that fed it.
void Sight_ReportEngineRays();
