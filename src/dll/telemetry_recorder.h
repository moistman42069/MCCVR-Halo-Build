#pragma once

#include "aim_pose_trace.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifdef HALOMCCVR_TELEMETRY_TESTING
#include <string>
#include <vector>
#endif

inline constexpr uint32_t kTelemetrySchemaVersion = 2;
inline constexpr uint32_t kTelemetryQueueSlots = 4096;
inline constexpr uint32_t kTelemetryQueueUsableCapacity =
    kTelemetryQueueSlots - 1;

enum class TelemetryRecorderState : uint8_t
{
    Unavailable = 0,
    Idle,
    Starting,
    Recording,
    Finalizing,
    Error,
};

enum class TelemetryErrorCode : uint8_t
{
    None = 0,
    InvalidLogDirectory,
    CreateControlEventFailed,
    CreateWorkerFailed,
    OpenFileFailed,
    WriteFailed,
    FlushFailed,
    CloseFailed,
    AllocationFailed,
    InternalFailure,
    CreateDirectoryFailed,
};

struct TelemetryVec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct TelemetryQuat
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct TelemetryPose
{
    TelemetryQuat orientation{};
    TelemetryVec3 position{};
};

struct TelemetryEyeView
{
    TelemetryPose pose{};
    float fovLeft = 0.0f;
    float fovRight = 0.0f;
    float fovUp = 0.0f;
    float fovDown = 0.0f;
};

struct TelemetryPadState
{
    // Packed 1-bit flags: the JSON contract is unchanged (same names and
    // meanings); only the in-memory representation is packed so TelemetryFrame
    // keeps its 2048-byte hot-path budget. Never take the address of a
    // bitfield member.
    uint64_t weaponPulseUntilMs = 0;
    bool valid : 1 = false;
    bool a : 1 = false;
    bool b : 1 = false;
    bool x : 1 = false;
    bool y : 1 = false;
    bool clickL : 1 = false;
    bool clickR : 1 = false;
    bool menu : 1 = false;
    bool thumbrestDpad : 1 = false;
    bool exclusiveInput : 1 = false;
    uint32_t weaponButtons = 0;
    uint32_t weaponGeneration = 0;
    float moveX = 0.0f;
    float moveY = 0.0f;
    float turnX = 0.0f;
    float turnY = 0.0f;
    float trigL = 0.0f;
    float trigR = 0.0f;
    float gripL = 0.0f;
    float gripR = 0.0f;
    float dpadX = 0.0f;
    float dpadY = 0.0f;
};

struct TelemetryEffectiveSettings
{
    // Bool flags are packed 1-bit bitfields (see TelemetryPadState); the
    // serialized effective_settings contract is unchanged.
    int32_t virtualStockRearReference = 0;
    uint8_t hybridDiagnosticOverride = 0;
    bool twoHandEnabled : 1 = false;
    bool twoHandLatched : 1 = false;
    bool twoHandToggle : 1 = true;
    bool virtualStockEnabled : 1 = false;
    bool horizontalReleaseEnabled : 1 = false;
    bool inverseNeckEnabled : 1 = false;
    bool proximityRelease : 1 = false;
    bool supportGripPoseEnabled : 1 = false;
    bool supportEndpointUsedGrip : 1 = false;
    bool leftHanded : 1 = false;
    bool crosshair : 1 = true;
    bool killReticle : 1 = true;
    float virtualStockStrength = 0.0f;
    float hybridOffhandInfluence = 0.0f;
    int32_t hybridAdsReference = 0;
    float hybridSeatFullM = 0.0f;
    float hybridSeatReleaseM = 0.0f;
    float horizontalReleaseFullM = 0.0f;
    float horizontalReleaseReleaseM = 0.0f;
    float inverseNeckStrength = 0.0f;
    float inverseNeckForwardM = 0.0f;
    float inverseNeckUpM = 0.0f;
    float inverseNeckLateralM = 0.0f;
    float rearHeightM = 0.0f;
    float shoulderBackM = 0.0f;
    float shoulderSideM = 0.0f;
    float chestHeightM = 0.0f;
    float chestBackM = 0.0f;
    float chestSideM = 0.0f;
    float proximityFullM = 0.0f;
    float proximityReleaseM = 0.0f;
    float gunYawDeg = 0.0f;
    float gunPitchDeg = 0.0f;
    float gunRollDeg = 0.0f;
    float aimStabilization = 0.0f;
    float crosshairDistanceM = 0.0f;
    float crosshairSizeDeg = 0.0f;
};

struct TelemetryAimResult
{
    bool valid : 1 = false;
    bool twoHandActive : 1 = false;
    bool rejectedExtreme : 1 = false;
    TelemetryPose pose{};
    TelemetryVec3 forward{};
    float rejectedAgreement = 0.0f;
};

struct TelemetryControlResult
{
    uint8_t profileId = 0;
    bool shoulderToHeadFallback : 1 = false;
    bool fixedTargetValid : 1 = false;
    bool fixedRearDistanceValid : 1 = false;
    bool fixedProximityEnabled : 1 = false;
    bool fixedProximityCalculated : 1 = false;
    bool fixedDirectionValid : 1 = false;
    bool exactAEndpointSelected : 1 = false;
    bool orientationRebuildAttempted : 1 = false;
    bool orientationRebuildSucceeded : 1 = false;
    uint8_t path = 0;
    uint8_t requestedTarget = 0;
    uint8_t actualTarget = 0;
    TelemetryEffectiveSettings effectiveSettings{};
    TelemetryAimResult aim{};
    TelemetryVec3 fixedTarget{};
    float fixedRearToTargetDistanceM = 0.0f;
    float fixedProximityInfluence = 0.0f;
    float fixedConfiguredStrength = 0.0f;
    float fixedEffectiveStrength = 0.0f;
};

// Engine aim/camera provenance (shots-vs-reticle frame tranche). Wire values
// are the uint8 ordinals below; None means the active title publishes no
// observation of that kind.
enum class TelemetryEngineAimSource : uint8_t
{
    None = 0,
    H3Shared = 1,                   // shared g_aimFwd, Halo 3 camera copy
    OdstShared = 2,                 // shared g_aimFwd, ODST camera copy
    ReachSeatedUnitAim = 3,         // Reach coherent seated native unit aim
    ReachSeatedCompactFallback = 4, // Reach seated, compact-camera fallback
    ReachOnFootCompactFallback = 5, // Reach on-foot shared compact fallback
    H4Observer = 6,                 // Halo 4 stereo-transaction observer
    H2Observer = 7,                 // Halo 2 observer stock camera
    H2Absent = 8,                   // Halo 2 active but no safe read
};

enum class TelemetryEngineCameraSource : uint8_t
{
    None = 0,
    H3 = 1,       // shared g_baseCam/g_cam, Halo 3 camera copy
    Odst = 2,     // shared g_baseCam/g_cam, ODST camera copy
    Reach = 3,    // Reach completed-frame eye
    H2 = 4,       // Halo 2 observer stock position
    H4Absent = 5, // Halo 4 drives but no usable stock camera position was
                  // published for this frame (before the first owned stereo
                  // transaction, or after teardown)
    H4Observer = 6, // Halo 4 stereo-transaction observer stock position
};

struct TelemetryFrame
{
    uint32_t schemaVersion = kTelemetrySchemaVersion;
    uint64_t preparedSerial = 0;
    int64_t predictedDisplayTime = 0;
    int64_t predictedDisplayPeriod = 0;
    int64_t captureBeginQpc = 0;
    int64_t captureEndQpc = 0;
    uint64_t contactSpaceEpoch = 0;
    int64_t contactSpaceChangeAtNs = 0;
    uint8_t activeTitle = 0;
    // Frame-direct validity/flag bits, packed (see TelemetryPadState). The
    // serialized frame contract is unchanged; never take the address of a
    // bitfield member. The block sits directly after activeTitle on purpose:
    // the T0 hot-path budget is exactly full (2048 bytes, static_assert
    // below), this placement spends the padding bytes a block after the
    // 4-byte session fields would leave unused, and that is what pays for the
    // free two-hand offhand-influence receipt. MSVC packs bool bitfields 8 per
    // byte, so this block occupies ceil(bit_count / 8) bytes; re-check
    // sizeof(TelemetryFrame) before moving members across it.
    bool shouldRender : 1 = false;
    bool upcomingViewsValid : 1 = false;
    bool focused : 1 = false;
    bool stereoEnabled : 1 = false;
    bool menuOpen : 1 = false;
    bool semanticPrimaryValid : 1 = false;
    bool semanticSupportValid : 1 = false;
    bool supportEndpointValid : 1 = false;
    bool supportEndpointUsedGrip : 1 = false;
    bool physicalLeftAimValid : 1 = false;
    bool physicalRightAimValid : 1 = false;
    bool supportGripValid : 1 = false;
    bool semanticPrimaryGripPositionValid : 1 = false;
    bool headSampleValid : 1 = false;
    bool stockHeadValid : 1 = false;
    bool semanticPrimaryVelocityValid : 1 = false;
    bool semanticSupportVelocityValid : 1 = false;
    bool testProfileCustom : 1 = true;
    bool transitionStockModeValid : 1 = false;
    bool transitionActive : 1 = false;
    bool twoHandTransitionSmoothingConfigured : 1 = false;
    bool twoHandTransitionActive : 1 = false;
    bool twoHandSmoothingConfigured : 1 = false;
    bool twoHandSmoothingApplied : 1 = false;
    bool transitionLiveCalibratedForwardValid : 1 = false;
    bool transitionPresentedForwardValid : 1 = false;
    bool transitionOneHandAnchorValid : 1 = false;
    bool twoHandLabEnabled : 1 = false;
    bool twoHandLabPrimaryPivotValid : 1 = false;
    bool twoHandLabSupportPivotValid : 1 = false;
    bool twoHandLabStatelessDirectionValid : 1 = false;
    bool twoHandLabPresentedDirectionValid : 1 = false;
    bool twoHandLabTemporalActive : 1 = false;
    // Shots-vs-reticle tranche bits (same packing; validity is authoritative).
    bool presentedAimValid : 1 = false;
    bool reticlePresentedValid : 1 = false;
    bool reticlePresentedSupportTrusted : 1 = false;
    bool engineAimValid : 1 = false;
    bool engineAimPitchValid : 1 = false;
    bool engineCameraBaseValid : 1 = false;
    bool engineCameraEyeValid : 1 = false;
    bool worldScaleValid : 1 = false;
    bool dualActive : 1 = false;
    // Persistent support grip (PG) frame tranche (same packing): frozen
    // solve-time provenance of the frame's own canonical assembly, never a
    // live re-read of mutable PG state. persistentSupportGripConfigured is the
    // config knob read for this frame; persistentSupportGripApplicable is the
    // pure title applicability gate for the active title. The four
    // relationship/solve bits are copied verbatim from the frame-local
    // AimPoseInputs the canonical solve consumed (supportSolveTrusted is the
    // qualification PERMISSION before consumption narrowing).
    bool persistentSupportGripConfigured : 1 = false;
    bool persistentSupportGripApplicable : 1 = false;
    bool supportRelationshipReadable : 1 = false;
    bool supportRelationshipEngaged : 1 = false;
    bool supportSolveTrusted : 1 = false;
    bool supportForceOneHand : 1 = false;
    int32_t sessionState = 0;
    uint32_t locatedViewCount = 0;

    TelemetryPose semanticPrimaryAim{};
    TelemetryVec3 semanticPrimaryForward{};
    TelemetryPose semanticSupportAim{};
    TelemetryVec3 supportEndpoint{};

    TelemetryPose physicalLeftAim{};
    TelemetryPose physicalRightAim{};

    TelemetryVec3 supportGripPosition{};
    TelemetryVec3 semanticPrimaryGripPosition{};

    TelemetryPose semanticHmd{};
    float headsetSmoothing = 0.0f;

    TelemetryEyeView views[2]{};
    // The two velocity vectors are declared together ahead of their sample
    // timestamps on purpose: the T0 budget is exactly full, and an
    // interleaved vec3/uint64 pair leaves a 4-byte alignment hole before each
    // timestamp (12-byte vec3 followed by an 8-byte member). Grouping keeps
    // both timestamps 8-aligned and spends no padding.
    TelemetryVec3 semanticPrimaryVelocity{};
    TelemetryVec3 semanticSupportVelocity{};
    uint64_t semanticPrimaryVelocityAtMs = 0;
    uint64_t semanticSupportVelocityAtMs = 0;
    TelemetryPadState pad{};

    uint8_t testProfileId = 0;
    TelemetryEffectiveSettings effectiveSettings{};
    AimPoseTrace aimTrace{};
    TelemetryAimResult canonicalAim{};

    // Grab/release aim continuity (Virtual Stock, Standard and Plus). This
    // family describes the presentation layer only; aim_trace.final_direction
    // and canonical_aim above stay the raw live solve. transition_stock_mode is
    // the product stock mode this frame (0 = Standard, 1 = Plus, i.e. rear
    // reference 3) and is only meaningful while transition_stock_mode_valid is
    // true (Virtual Stock enabled); when invalid it reads 0, never a stale
    // mode.
    uint8_t transitionStockMode = 0;
    uint8_t transitionPhase = 0;
    uint8_t transitionEdgeKind = 0;
    uint8_t transitionAnchorSource = 0;
    TelemetryVec3 transitionLiveCalibratedForward{};
    TelemetryVec3 transitionPresentedForward{};
    float transitionInitialCorrectionDeg = 0.0f;
    float transitionRemainingCorrectionDeg = 0.0f;
    float transitionElapsedMs = 0.0f;
    TelemetryVec3 transitionOneHandAnchorForward{};
    uint64_t transitionAdvanceCount = 0;
    uint64_t transitionLastPreparedSerial = 0;
    // The prepared serial the layer actually ran for; 0 when it did not run
    // (feature off/inactive), so absence is unambiguous.
    uint64_t transitionAppliedSerial = 0;
    // Two-Hand Smoothing user strength (0..25; 0 = raw/off controller input,
    // 25 = the full fixed speed-25 filter): the value FROZEN for this prepared
    // serial that the solve consumed, never a live re-read of the config
    // slider. The wet/dry mix is emitted by the serializer as strength/25 from
    // this same frozen value; the T0 frame budget leaves no room for a second
    // float and the mix is a pure function of the strength.
    float twoHandSmoothingStrength = 0.0f;
    // Free two-hand (VS-OFF) product offhand directional authority (0..1),
    // FROZEN for this prepared serial from the frame-local assembly the solve
    // consumed (AimPoseInputs.twoHandOffhandInfluence, stamped by
    // CurrentStockAimPoseInputs from `two_hand_offhand_influence`), never a
    // live re-read of the config slider at capture. The recorded value is what
    // the VS-OFF free two-hand solver consumes: 0 = the primary controller's
    // own directional aim is authoritative, 1 = the accepted support direction
    // owns presentation, non-finite reads 0 and every other value is clamped
    // to [0,1] exactly as the solver clamps it. Virtual Stock solves never read
    // it, and a Lab-active solve consumes its own twoHandLabOffhandInfluence
    // instead (reported by the two_hand_lab_* family).
    float twoHandOffhandInfluence = 0.0f;
    // Fixed Pavlov-inspired input-filter response and raw-to-full-filtered
    // errors. Angular error is degrees; both position errors are OpenXR local
    // metres. alpha is the filter's INTERNAL clamp(25*dt,0,1) temporal
    // coefficient, never the user-facing strength amount.
    float twoHandSmoothingAlpha = 0.0f;
    float twoHandSmoothingPrimaryOrientationErrorDeg = 0.0f;
    float twoHandSmoothingPrimaryPositionErrorM = 0.0f;
    float twoHandSmoothingSupportPositionErrorM = 0.0f;

    // Two-Hand Lab (additive schema-2 family, same rules as transition_*:
    // every existing field meaning is unchanged, absence in older recordings
    // is unambiguous, the validator accepts absence as legacy while checking
    // every present field and cross-field invariant). The frame's stateless
    // Lab observation comes from the canonical solve's trace Lab payload;
    // the presented orientation comes from the Lab temporal packet/state for
    // this same serial — never recomputed geometry in the recorder.
    // twoHandLabEnabled means the Lab steered this frame's canonical solve
    // (trace labValid); while false every other field of the family reads
    // its inactive default (enums 0, floats 0, validity false, zero vectors),
    // never a stale observation.
    uint8_t twoHandLabAnchorRequested = 0;
    uint8_t twoHandLabAnchorResolved = 0;
    uint8_t twoHandLabAnchorFallback = 0;
    float twoHandLabOffhandInfluence = 0.0f;
    uint8_t twoHandLabAgreementMode = 0;
    float twoHandLabAgreement = 0.0f;
    float twoHandLabAgreementConfidence = 0.0f;
    float twoHandLabEffectiveInfluence = 0.0f;
    uint8_t twoHandLabTemporalMode = 0;
    TelemetryVec3 twoHandLabPrimaryPivot{};
    TelemetryVec3 twoHandLabSupportPivot{};
    TelemetryVec3 twoHandLabStatelessDirection{};
    TelemetryVec3 twoHandLabPresentedDirection{};
    float twoHandLabTemporalErrorDeg = 0.0f;

    TelemetryControlResult cfVsOff{};
    TelemetryControlResult cfFixedHead{};
    TelemetryControlResult cfFixedShoulder{};

    // ---- Shots-vs-reticle frame tranche (additive schema 2) ----
    // presentedAimForward: same-frame reconstruction of the composed
    // presented/steering orientation (VR_GetAimPoseWithSupportProvenance
    // composition: frame-local solve, then VS continuity, then Lab temporal),
    // as an OpenXR LOCAL forward. Valid only when every presentation stage is
    // provably the getter's output for this frame's assembly; otherwise false
    // and the transition_*/two_hand_lab_* piece fields carry the evidence.
    TelemetryVec3 presentedAimForward{};
    // reticlePresented_*: the exact consumer-visible reticle pose with its own
    // serial. Capture runs BEFORE the reticle block publishes in the same
    // frame, so this is normally the PREVIOUS serial's pose; its own serial
    // travels in reticlePresentedSerial. Never assert equality with
    // preparedSerial. Orientation/position are OpenXR LOCAL.
    uint64_t reticlePresentedSerial = 0;
    uint64_t reticlePresentedSampleMs = 0;
    uint64_t reticlePresentedSupportEpoch = 0;
    TelemetryQuat reticlePresentedOrientation{};
    TelemetryVec3 reticlePresentedPosition{};
    // engineAim_*: per-title engine aim feedback in ENGINE-WORLD (Blam Z-up,
    // world units), NOT OpenXR LOCAL. engineAimSource names the exact
    // publication (TelemetryEngineAimSource); serial 0 means latest-only with
    // no serial; sampleMs 0 means none. engineAimPitchDeg is degrees, valid
    // only when an engine pitch publication backs it (Halo 4).
    TelemetryVec3 engineAimForward{};
    uint64_t engineAimSerial = 0;
    uint64_t engineAimSampleMs = 0;
    float engineAimPitchDeg = 0.0f;
    uint8_t engineAimSource = 0;
    // engineCamera_*: engine camera truth in ENGINE-WORLD units. Base is the
    // pre-lean origin (H3/ODST shared g_baseCam); eye is the rendered camera
    // (H3/ODST shared g_cam, Reach completed eye, H2 observer stock).
    // engineCameraSource names the writer (TelemetryEngineCameraSource);
    // engineCameraSerial carries the publication serial where one exists
    // (Reach preparedSerial, H2 observer serial), else 0. worldScale is engine
    // world units per metre under the active title's authority (H3/H4 live
    // g_worldScale; Reach/ODST/H2 constants; invalid elsewhere).
    TelemetryVec3 engineCameraBasePosition{};
    TelemetryVec3 engineCameraEyePosition{};
    uint64_t engineCameraSerial = 0;
    float worldScale = 0.0f;
    uint8_t engineCameraSource = 0;

    // Persistent support grip (PG) frame tranche payload. Both are plain
    // unsigned integers so the all-ones epoch sentinel
    // (support_grip::kUnknownRelationshipEpoch == 2^64-1) round-trips exactly
    // as an integer, never as a float or a string. supportEpoch is the frozen
    // relationship epoch the assembly read (0 = coherently disengaged,
    // all-ones = the relationship could not be read); supportSolveSerial is
    // the prepared serial the assembly stamped for this solve, which at the
    // capture site is the last published prepared serial (normally
    // preparedSerial - 1, and never asserted equal to it).
    uint64_t supportEpoch = 0;
    uint64_t supportSolveSerial = 0;
};

static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::is_trivially_copyable_v<TelemetryFrame>);
static_assert(std::is_standard_layout_v<TelemetryFrame>);
static_assert(sizeof(TelemetryFrame) <= 2048,
              "TelemetryFrame grew beyond the T0 hot-path budget");
struct TelemetryStatusSnapshot
{
    TelemetryRecorderState state = TelemetryRecorderState::Unavailable;
    TelemetryErrorCode error = TelemetryErrorCode::None;
    uint32_t systemError = 0;
    bool desiredRecording = false;
    bool accepting = false;
    int64_t sessionStartQpc = 0;
    int64_t qpcNow = 0;
    int64_t qpcFrequency = 0;
    uint64_t producerCalls = 0;
    uint64_t duplicateSerialSuppressed = 0;
    uint64_t enqueued = 0;
    uint64_t written = 0;
    uint64_t droppedQueueFull = 0;
    uint64_t writerFailures = 0;
    uint64_t weaponEventsEnqueued = 0;
    uint64_t weaponEventsWritten = 0;
    uint64_t weaponEventsDroppedQueueFull = 0;
};

// ---- Weapon-order diagnostic events (read-only evidence tranche) ----
// Fixed-size, trivially copyable event records for the persistent-support-grip
// first-B-frame ordering question. Transported by the telemetry recorder's own
// worker/file pipeline (same JSONL file, same analyser); no second recorder.
inline constexpr uint32_t kWeaponOrderEventQueueSlots = 1024;
// Sentinel for a shot payload field the firing context does not carry
// (serialized as -1, never as a fabricated 0/1 identity).
inline constexpr uint8_t kTelemetryShotIndexUnknown = 0xFF;

enum class WeaponOrderEventKind : uint8_t
{
    PresentBegin = 0,
    AfterPresentBeforePrepare,
    CapturePreLatch,
    CaptureProbeBegin,
    CaptureProbeResult,
    FpEntry,
    FpWeaponCommit,
    Shot,
};

enum class WeaponOrderEventStatus : uint8_t
{
    NoObservation = 0,
    Success,
    ReaderReturnedFalse,
    GuardRejected,
    ExceptionOrFault,
    NotAttemptedNoSafeThread,
    NotAttemptedThreadMismatch,
    DefinitivelyAbsent,
};

struct TelemetryWeaponEvent
{
    uint64_t sequence = 0; // process-wide diagnostic order, 0 = slot free
    uint64_t session = 0; // recorder admission generation at publish
    int64_t timestampQpc = 0;
    uint32_t threadId = 0;
    uint8_t kind = 0;
    uint8_t status = 0;
    uint8_t title = 0;
    uint32_t titleGeneration = 0;
    uint64_t preparedSerial = 0;
    uint32_t controlledUnit = 0;
    uint32_t primaryWeapon = 0;
    // Per-kind meaning: CaptureProbeResult carries aux0 = matching
    // CaptureProbeBegin sequence (0 when no Begin was emitted) and aux0/aux1
    // otherwise carry title-defined validity/provenance bits. Shot carries
    // aux0 = aux1 = 0 (reserved) and the fixed shot payload below.
    uint64_t aux0 = 0;
    uint64_t aux1 = 0;
    // Fixed T-2 shot payload, meaningful only for kind Shot (zero otherwise).
    // All four vectors are finite on the wire (non-finite serializes as null
    // but producers suppress a non-finite final ray instead of publishing
    // it). shotOrigin/shotDirection are the final ray the engine consumes,
    // in engine-world units. shotEngineAim is the title's engine aim feedback
    // snapshot (zeros when the bounded read was unavailable).
    // shotReticleDirection is the consumer-visible reticle forward in
    // OpenXR LOCAL (zeros when the publication was unavailable).
    // shotSlot/shotBarrel are 0/1 where the firing context carries them and
    // kTelemetryShotIndexUnknown (0xFF) when it does not; the wire value is
    // -1 for unknown so every serialized value is a small signed integer
    // (shot_slot -1..1, shot_barrel -1..1). shotFlags bits (unknown = 0):
    //   bit0 predicted (the title's fire call carried prediction),
    //   bit1 substituted (the mod's substitution produced the final ray),
    //   bit2 vrAimActive,
    //   bit3 twoHandActive (bounded shared-state read, never the aim getter),
    //   bit4 leftHanded (captured handedness),
    //   bit5 dualActive (shared dual-presentation predicate),
    //   bit6 firesFromCamera (when the title's call carries it),
    //   bit7 unitAim (when the title's call carries it).
    // shotEngineAimSource uses the TelemetryEngineAimSource ordinals (the same
    // per-title vocabulary as the frame's engine_aim_source), but for this
    // event 0 means "no engine aim snapshot was available for this shot". The
    // snapshot is reported only when the title read succeeds AND the direction
    // is usable by the frame channel's rule (finite, non-zero length);
    // otherwise the source stays 0 (or 8 for Halo 2 absent) and the vector is
    // exactly zero.
    //   shot_engine_aim_source: 0 none, 1 H3 shared, 2 ODST shared,
    //   3 Reach seated unit, 4 Reach seated compact, 5 Reach on-foot compact,
    //   6 H4 observer, 7 H2 observer, 8 H2 absent.
    // Direction-absent producers (ODST's firing-origin hook carries no
    // direction) publish an exact zero direction; the origin is still the
    // final ray origin the engine consumed.
    float shotOrigin[3]{};
    float shotDirection[3]{};
    float shotEngineAim[3]{};
    float shotReticleDirection[3]{};
    uint8_t shotSlot = 0;
    uint8_t shotBarrel = 0;
    uint16_t shotFlags = 0;
    uint8_t shotEngineAimSource = 0;
    uint64_t tearGuard = 0; // must equal sequence once the slot is committed
};

static_assert(std::is_trivially_copyable_v<TelemetryWeaponEvent>);
static_assert(std::is_standard_layout_v<TelemetryWeaponEvent>);

struct TelemetryWeaponEventCounters
{
    uint64_t enqueued = 0;
    uint64_t written = 0;
    uint64_t droppedQueueFull = 0;
    uint64_t sessionSkipped = 0;
};

// Cheap recorder-active gate for diagnostic probes. When false, probes must
// perform no diagnostic reads, allocate nothing and publish nothing.
bool Telemetry_WeaponEventsAccepting() noexcept;
// Admission generation of the live recording session (0 when not accepting).
// Diagnostic metadata is valid only within the session that produced it.
uint64_t Telemetry_CurrentSessionToken() noexcept;
// Publishes one fixed event record. Returns the assigned global sequence, or
// 0 when recording is off or the record was dropped (queue full). Never
// allocates, logs, takes locks, performs I/O/COM/scans, or mutates gameplay.
uint64_t Telemetry_PublishWeaponEvent(uint8_t kind, uint8_t status,
    uint8_t title, uint32_t titleGeneration, uint64_t preparedSerial,
    uint32_t controlledUnit, uint32_t primaryWeapon, uint64_t aux0,
    uint64_t aux1) noexcept;
// T-2 shot payload for kind Shot. Field-for-field copy of the fixed shot
// payload in TelemetryWeaponEvent; same admission/sequence/gap semantics and
// the same disabled gate (one atomic load, zero diagnostic work when off).
// slot/barrel accept kTelemetryShotIndexUnknown for "the firing context does
// not carry it" (serialized as -1).
struct TelemetryShotPayload
{
    float origin[3]{};
    float direction[3]{};
    float engineAim[3]{};
    float reticleDirection[3]{};
    uint8_t slot = 0;
    uint8_t barrel = 0;
    uint16_t flags = 0;
    // TelemetryEngineAimSource ordinal for engineAim; 0 means the snapshot was
    // unavailable or unusable (read failure, or a direction the frame
    // capture's usability rule rejects), and engineAim is then exactly zero.
    // Source 8 (Halo 2 active without a usable observer publication) is the
    // same no-snapshot label in the Halo 2 branch and likewise carries an
    // exactly-zero vector.
    uint8_t engineAimSource = 0;
};
uint64_t Telemetry_PublishShotEvent(uint8_t status, uint8_t title,
    uint32_t titleGeneration, uint64_t preparedSerial, uint32_t controlledUnit,
    uint32_t primaryWeapon, const TelemetryShotPayload& shot) noexcept;
TelemetryWeaponEventCounters Telemetry_GetWeaponEventCounters() noexcept;

bool Telemetry_Init() noexcept;
void Telemetry_RequestStart() noexcept;
void Telemetry_RequestStop() noexcept;
TelemetryStatusSnapshot Telemetry_GetStatus() noexcept;
const char* Telemetry_StateName(TelemetryRecorderState state) noexcept;
const char* Telemetry_ErrorName(TelemetryErrorCode error) noexcept;

// Returns false on the disabled path, a Start/Stop race, a duplicate serial, or
// a full queue. Queue-full rejection happens before the caller captures a frame.
// A true return owns one producer-in-flight receipt that must be completed by
// exactly one Telemetry_PublishFrame call.
bool Telemetry_BeginFrame(uint64_t preparedSerial) noexcept;
void Telemetry_PublishFrame(const TelemetryFrame& frame) noexcept;

#ifdef HALOMCCVR_TELEMETRY_TESTING
void Telemetry_TestForceWeaponEventsAccepting(bool enabled) noexcept;
void Telemetry_TestResetWeaponEvents() noexcept;
bool Telemetry_TestPopWeaponEvent(TelemetryWeaponEvent& out) noexcept;
uint64_t Telemetry_TestWeaponEventDrops() noexcept;
bool Telemetry_TestSerializeWeaponEvent(const TelemetryWeaponEvent& event,
    char* output, size_t capacity, size_t& written) noexcept;
// Runs the production worker drain core through an in-memory sink with the
// supplied session token. Exposes gap markers and cross-session skips exactly
// as the real worker file path emits them.
bool Telemetry_TestDrainWeaponEvents(uint64_t sessionToken,
    std::vector<std::string>& lines) noexcept;
// Same, but the sink fails after `maxLines` successful lines. Used to prove a
// failed write does not advance the drain pointer and the event is retried.
bool Telemetry_TestDrainWeaponEventsLimited(uint64_t sessionToken,
    size_t maxLines, std::vector<std::string>& lines) noexcept;
// Exercises the session final-drain semantics (producer quiescence assumed):
// every claimed sequence up to the frontier must be serialized or explicitly
// gapped before the session may close cleanly.
bool Telemetry_TestFinalDrainWeaponEvents(uint64_t sessionToken,
    std::vector<std::string>& lines) noexcept;
// Weapon-event producer-in-flight handshake hooks (transport tests only).
uint32_t Telemetry_TestWeaponEventProducersInFlight() noexcept;
void Telemetry_TestPauseWeaponEventProducer(bool enabled) noexcept;
bool Telemetry_TestWeaponEventProducerReachedPause() noexcept;
// Test-only pause after an accepted sequence claim and before its commit
// marker: creates a genuinely outstanding claim for drain-safety tests.
void Telemetry_TestPauseWeaponEventCommit(bool enabled) noexcept;
bool Telemetry_TestWeaponEventCommitReachedPause() noexcept;
void Telemetry_TestBumpAdmissionGeneration() noexcept;
void Telemetry_TestResetAdmissionToken() noexcept;
void Telemetry_TestResetRing() noexcept;
bool Telemetry_TestPushRing(const TelemetryFrame& frame) noexcept;
bool Telemetry_TestPopRing(TelemetryFrame& frame) noexcept;
uint64_t Telemetry_TestRingDrops() noexcept;
void Telemetry_TestSetForceOpenFailure(bool enabled) noexcept;
void Telemetry_TestFailWriteAfter(int32_t successfulWrites) noexcept;
void Telemetry_TestFailFlushAfter(int32_t successfulFlushes) noexcept;
void Telemetry_TestSetForceCloseFailure(bool enabled) noexcept;
void Telemetry_TestPauseStarting(bool enabled) noexcept;
void Telemetry_TestPauseRecording(bool enabled) noexcept;
bool Telemetry_TestWorkerReachedRecording() noexcept;
void Telemetry_TestPauseRecordingDrain(bool enabled) noexcept;
bool Telemetry_TestWorkerReachedRecordingDrain() noexcept;
void Telemetry_TestPauseFinalizing(bool enabled) noexcept;
void Telemetry_TestPauseProducerAdmission(bool enabled) noexcept;
bool Telemetry_TestProducerReachedAdmission() noexcept;
void Telemetry_TestPauseProducerSecondCheck(bool enabled) noexcept;
bool Telemetry_TestProducerReachedSecondCheck() noexcept;
bool Telemetry_TestSerializeFrame(
    const TelemetryFrame& frame, char* output, size_t capacity,
    size_t& written) noexcept;
bool Telemetry_TestEscapeJson(
    const char* value, char* output, size_t capacity,
    size_t& written) noexcept;
struct TelemetryAnalyserLaunchPlanTest
{
    std::wstring scriptPath;
    std::wstring recordingPath;
    std::wstring commandLine;
};
struct TelemetryAnalyserLauncherTestSnapshot
{
    uint32_t launchRequests = 0;
    uint32_t launchCompletions = 0;
    uint32_t pythonResolutionAttempts = 0;
    uint32_t pyResolutionAttempts = 0;
    uint32_t pythonProcessAttempts = 0;
    uint32_t pyProcessAttempts = 0;
    TelemetryRecorderState stateAtQueue =
        TelemetryRecorderState::Unavailable;
    TelemetryRecorderState stateAtLastProcessAttempt =
        TelemetryRecorderState::Unavailable;
    bool sessionClosedAtLastProcessAttempt = false;
    bool standardHandlesValid = false;
    bool inheritHandles = false;
    bool restrictedHandleList = false;
    uint32_t creationFlags = 0;
    std::wstring applicationName;
    std::wstring commandLine;
    std::wstring recordingPath;
};
std::wstring Telemetry_TestQuoteWindowsArgument(const wchar_t* value);
std::vector<wchar_t> Telemetry_TestBuildUtf8Environment(
    const wchar_t* environmentBlock);
bool Telemetry_TestBuildAnalyserLaunchPlan(
    const wchar_t* moduleDirectory, const wchar_t* recordingPath,
    const wchar_t* interpreterPath, bool pyLauncher,
    TelemetryAnalyserLaunchPlanTest& plan);
void Telemetry_TestResetAnalyserLauncher();
void Telemetry_TestConfigureAnalyserLauncher(
    bool pythonAvailable, bool pyAvailable,
    bool pythonLaunchSucceeds, bool pyLaunchSucceeds);
void Telemetry_TestSetAnalyserModuleDirectory(const wchar_t* directory);
// Reports the recording directory the recorder itself constructed, so tests
// assert on the implementation's path rather than a locally assembled string.
const wchar_t* Telemetry_TestRecordingDirectoryPath() noexcept;
void Telemetry_TestUseRealAnalyserLauncher(bool enabled) noexcept;
void Telemetry_TestPauseAnalyserLaunch(bool enabled) noexcept;
bool Telemetry_TestAnalyserLaunchReached() noexcept;
void Telemetry_TestLaunchAnalyser(
    const wchar_t* moduleDirectory, const wchar_t* recordingPath) noexcept;
uint32_t Telemetry_TestAnalyserLaunchCompletions() noexcept;
TelemetryAnalyserLauncherTestSnapshot
Telemetry_TestGetAnalyserLauncherSnapshot();
#endif
