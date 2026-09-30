#pragma once
#include <cstdint>
#include "runtime_types.h"

// Persistent support grip: durable relationship ownership + per-invocation
// owner trust. Pure policy only; no engine state, no publication, no I/O.
//
// The existing UpdateTwoHandHold() keeps ownership of spatial admission and
// logical retention. This module adds the decisions the old boolean latch
// could not make:
//
//   1. DURABLE  -- which concrete weapon incarnation the relationship belongs
//      to, and when proven owner evidence must terminate it.
//   2. INVOCATION -- whether one currently executing weapon invocation may
//      consume support semantics at all. Title callbacks read this; they never
//      mutate the durable relationship.
//
// Owner evidence is tri-state. Unknown is never proof of absence.
//
// User-requested weapon-switch policy (2026-09-28, PG ON only):
//   * a proven KnownPresent owner replacement drops the relationship WITHOUT
//     arming release-before-reacquire, so a still-held Grip can re-acquire the
//     new weapon through the ORDINARY spatial admission (pending/bind path,
//     F08 freshness floor intact) and a toggle click can bind it again;
//   * that drop also requires a FRESH ZONE CROSSING before hold mode may
//     re-acquire (headset follow-up, same day): through a weapon swap the
//     support hand is usually still inside the NEW weapon's grab zone, and the
//     level-triggered `held && inZone` edge re-bound the replacement on the next
//     frame, so the switched weapon arrived pre-held. The grab point must leave
//     the zone at least once - DefaultSwitchReplacementDrop /
//     StepSwitchZoneReentry - and no Grip release/press cycle is required;
//   * the optional `two_hand_switch_inherit` instead rebinds the relationship
//     in place to a complete, current-generation replacement owner, so the
//     switched weapon is two-handed immediately with no re-orientation. It
//     never arms the crossing requirement;
//   * a proven absence and every generation/lifecycle/role/gesture teardown
//     keep arming release-before-reacquire.
//
// Repairs carried into this port (A017 red-team F01/F02/F08/F12/F14):
//   * the owner tuple carries the game title: a Halo 3 weapon incarnation and
//     a numerically identical Reach datum are not the same owner;
//   * durable lifecycle identity (F02): a title change, an unavailable title
//     slot or generation, or a Shell/Loading/Dead runtime mode terminates an
//     engaged relationship (and arms release-before-reacquire), while Paused
//     and temporary tracking/evidence outages are retained;
//   * fresh acquisition carries an evidence revision floor so a cached
//     KnownPresent from the previous owner can never bind a new relationship;
//   * a pending acquisition intent survives a toggle button-up and is
//     cancelled only by an explicit toggle/cancel or a lifecycle/title/
//     generation/role/secondary invalidation (hold mode lives while Grip is
//     held);
//   * PresentedRayOwnerCompatible() no longer accepts an epoch-0 ray unless
//     the caller explicitly asserts that the relationship was coherently
//     readable and disengaged when the pose was solved.
//
// Repairs carried into this port by the title-wiring tranches (T6a/T6b):
//   * the per-title rollout gate: only a title whose producer, trust seam and
//     fallback are wired may consume the feature (T6a);
//   * F03 absence without producer agreement in every wired title reader;
//   * F06/F07/F14 consumer qualification: a support-sensitive consumer of the
//     presented ray must have BOTH a compatible producer receipt (owner, epoch,
//     serial staleness, F14 epoch-0 coherence) AND a title-native
//     current-invocation owner receipt for the consuming frame. Epoch equality
//     alone is never sufficient, and blind serial equality is deliberately not
//     required (a completed ray may be one serial old).
//   * SupportRayConsumable()'s donor epoch-0 short-circuit is gone: an epoch-0
//     ray is accepted only when the caller asserts the relationship was
//     coherently readable and disengaged (the same discipline as
//     PresentedRayOwnerCompatible).
namespace support_grip
{

enum class OwnerEvidence : uint8_t
{
    Unknown = 0,
    KnownPresent = 1,
    KnownAbsent = 2,
};

// F01: the owner identity is title + title generation + controlled unit +
// full weapon handle. Title generations are per-title slots, not a
// process-global incarnation counter, so two titles can legitimately both be
// at generation 1 with colliding numeric handles.
struct OwnerTuple
{
    GameTitle title = GameTitle::None;
    uint32_t generation = 0;
    uint32_t unit = 0xFFFFFFFFu;
    uint32_t weapon = 0xFFFFFFFFu;
};

// A title slot can identify an owner only when it is a real gameplay title;
// None (no title) and Unknown (unclassified) are not owner identities.
constexpr bool TitleIdentifiesOwner(GameTitle title) noexcept
{
    return title != GameTitle::None && title != GameTitle::Unknown;
}

constexpr bool OwnerComplete(const OwnerTuple& owner) noexcept
{
    return TitleIdentifiesOwner(owner.title) &&
        owner.generation != 0 &&
        owner.unit != 0 && owner.unit != 0xFFFFFFFFu &&
        owner.weapon != 0 && owner.weapon != 0xFFFFFFFFu;
}

constexpr bool SameOwner(const OwnerTuple& a, const OwnerTuple& b) noexcept
{
    return a.title == b.title && a.generation == b.generation &&
        a.unit == b.unit && a.weapon == b.weapon;
}

// Transitional rollout / failure-isolation gate. Only a title whose owner
// producer, current-invocation trust seam and one-hand fallback are wired may
// consume the feature; every other title keeps EXACT base behaviour even when
// the config is on (the writer runs its ordinary latch path, no durable state
// is published and the aim assembly is the untouched base assembly). This is
// deliberate during the port: a title that cannot prove its owner must never be
// exposed to a half-wired relationship.
//
// Wired: every gameplay title. Halo3/HaloCE/Halo2 landed in T6a, Halo3ODST and
// HaloReach in T6b, and Halo4 in T6c (producer + storm-hands trust boundary +
// owner-safe one-hand fallback + presentation). None/Unknown stay outside.
constexpr bool PersistentSupportGripApplies(GameTitle title) noexcept
{
    return title == GameTitle::Halo3 || title == GameTitle::HaloCE ||
        title == GameTitle::Halo2 || title == GameTitle::Halo3ODST ||
        title == GameTitle::HaloReach || title == GameTitle::Halo4;
}

// Titles whose FP interpolate hook publishes a title-native invocation receipt
// (VR_PublishSupportInvocationReceipt) for the consuming-frame half of the F06
// presented-ray qualification. Reach is the presented-reticle consumer wired by
// this tranche. Halo 3's per-pair decision is thread-local to its render thread
// and is deliberately not published (T5b note), and CE/Halo 2 keep their own
// packet-scoped receipts, so their presented rays keep the solve-time receipt
// until their consumer seam lands. This is a source-provable list: a title
// appears here only while its hook actually calls the publisher.
constexpr bool TitlePublishesSupportInvocationReceipt(GameTitle title) noexcept
{
    return title == GameTitle::HaloReach;
}

// A durable transition the single writer must apply to the relationship.
enum class DurableEvent : uint8_t
{
    None = 0,
    Bind = 1,        // acquisition bound to a proven current owner
    Invalidated = 2, // proven owner replacement/absence: drop the relationship
    Released = 3,    // explicit input release / lifecycle teardown
};

// Durable relationship step. `owner` is the relationship's current owner
// (in/out: only rewritten by Bind). `acquisitionRequested` is the ordinary
// hold/toggle intent produced by the existing admission path, already
// qualified by the pending-intent freshness gate (see
// PendingAcquisitionRequest) when a fresh bind is being attempted; a requested
// acquisition with no proven owner returns None so the writer simply waits
// (held/in-zone acquisition retries on following updates).
inline DurableEvent DurableRelationshipStep(
    bool engaged, bool acquisitionRequested,
    OwnerEvidence evidence, const OwnerTuple& evidenceOwner,
    OwnerTuple& owner) noexcept
{
    if (engaged)
    {
        if (evidence == OwnerEvidence::KnownAbsent)
            return DurableEvent::Invalidated;
        if (evidence == OwnerEvidence::KnownPresent &&
            !SameOwner(evidenceOwner, owner))
            return DurableEvent::Invalidated;
        return DurableEvent::None;
    }
    if (!acquisitionRequested)
        return DurableEvent::None;
    if (evidence == OwnerEvidence::KnownPresent && OwnerComplete(evidenceOwner))
    {
        owner = evidenceOwner;
        return DurableEvent::Bind;
    }
    return DurableEvent::None;
}

// Durable writer policy. The single OpenXR input writer owns the concrete
// relationship; this state machine is the one place the transition order is
// expressed, and the production writer steps it directly so a test exercises
// the same policy:
//
//   generation replacement -> lifecycle gate -> owner break -> bind
//
// `engaged` is the shared two-hand engagement bit. `epoch` changes only on a
// Bind/Invalidated/Released transition, never on an inconclusive step.
// `requiresRelease` is the release-before-reacquire admission state: only a
// proven ABSENCE or a generation replacement arms it. A KnownPresent owner
// replacement drops the relationship without arming, so the held/in-zone path
// (or the next toggle click) may re-acquire the new owner through ordinary
// spatial admission and the F08 freshness floor; with the wired title's
// `two_hand_switch_inherit` option the replacement is instead rebound in place
// without arming anything.
struct DurableWriterState
{
    bool engaged = false;
    OwnerTuple owner{};
    uint64_t epoch = 1;
    bool requiresRelease = false;
};

enum class DurableWriterAction : uint8_t
{
    None = 0,
    Bind = 1,
    Invalidated = 2,
    Released = 3,
};

// One writer step. `lifecycleGateOk` is the AND of every hard gate that must
// hold for the relationship to continue (grab admission, two-hand config,
// tracking validity, weapon gesture). `acquisitionRequested` is the ordinary
// hold/toggle intent; a request without a proven current owner returns None so
// the writer simply waits (held/in-zone acquisition retries on later updates).
// `evidenceOwner` must be generation-complete and current to bind.
// `inheritReplacement` is the wired title's `two_hand_switch_inherit` option:
// when set, a proven owner replacement that is complete and belongs to the
// active generation rebinds the relationship in place to the new owner
// (epoch++, still engaged, no arming) instead of dropping it, so the switched
// weapon inherits the two-hand hold immediately. It is deliberately gated by
// that option and only ever applies to a PROVEN replacement: an incomplete,
// foreign-title or stale-generation claim falls back to the default
// invalidation, and a proven absence still arms release-before-reacquire.
inline DurableWriterAction DurableWriterStep(DurableWriterState& state,
    bool lifecycleGateOk, bool acquisitionRequested, OwnerEvidence evidence,
    const OwnerTuple& evidenceOwner, uint32_t activeGeneration,
    bool inheritReplacement = false) noexcept
{
    // 0) A title/level generation replacement terminates the relationship
    //    even while owner evidence is unavailable: a held Grip must not
    //    survive a reload with the previous owner identity.
    if (state.engaged && activeGeneration != 0 && state.owner.generation != 0 &&
        state.owner.generation != activeGeneration)
    {
        state.requiresRelease = true;
        state.owner = {};
        state.epoch++;
        state.engaged = false;
        return DurableWriterAction::Invalidated;
    }
    // 1) Lifecycle gates terminate the relationship without arming the
    //    release-before-reacquire policy.
    if (!lifecycleGateOk)
    {
        if (state.engaged)
        {
            state.owner = {};
            state.epoch++;
            state.engaged = false;
        }
        return DurableWriterAction::Released;
    }
    // 2) A proven owner break runs BEFORE ordinary acquisition. A KnownPresent
    //    replacement drops the relationship WITHOUT arming: the new owner is
    //    admitted through the ordinary spatial admission + pending-intent +
    //    F08 freshness path, so a held Grip re-acquires it in place (hold
    //    mode) or the next click binds it (toggle mode). A proven ABSENCE
    //    still arms release-before-reacquire: a held Grip must not silently
    //    bind whatever owner the engine reaches while the weapon slot is
    //    empty. With `inheritReplacement` a complete, current-generation
    //    replacement owner is instead rebound in place, which is a proven
    //    replacement rather than a fresh acquisition, so no F08 floor applies
    //    (there is no cached prior-owner record to exclude: the claim is the
    //    replacement owner itself).
    if (state.engaged)
    {
        if (evidence == OwnerEvidence::KnownAbsent)
        {
            state.requiresRelease = true;
            state.owner = {};
            state.epoch++;
            state.engaged = false;
            return DurableWriterAction::Invalidated;
        }
        if (evidence == OwnerEvidence::KnownPresent &&
            !SameOwner(evidenceOwner, state.owner))
        {
            if (inheritReplacement && OwnerComplete(evidenceOwner) &&
                evidenceOwner.title == state.owner.title &&
                evidenceOwner.generation == activeGeneration)
            {
                state.owner = evidenceOwner;
                state.epoch++;
                state.engaged = true;
                return DurableWriterAction::Bind;
            }
            state.owner = {};
            state.epoch++;
            state.engaged = false;
            return DurableWriterAction::Invalidated;
        }
        return DurableWriterAction::None;
    }
    // 3) Acquisition binds a proven current owner only.
    if (acquisitionRequested && evidence == OwnerEvidence::KnownPresent &&
        OwnerComplete(evidenceOwner) &&
        evidenceOwner.generation == activeGeneration)
    {
        state.owner = evidenceOwner;
        state.epoch++;
        state.engaged = true;
        return DurableWriterAction::Bind;
    }
    return DurableWriterAction::None;
}

// Explicit input release: clear the owner and advance the epoch.
// `requiresRelease` is deliberately untouched - it is the caller's admission
// state and is only armed by a proven absence / generation replacement.
inline DurableWriterAction DurableWriterRelease(
    DurableWriterState& state) noexcept
{
    state.owner = {};
    state.epoch++;
    state.engaged = false;
    return DurableWriterAction::Released;
}

// ---------------------------------------------------------------------------
// 2026-09-28 headset follow-up: the default weapon-switch re-grip UX
// ---------------------------------------------------------------------------

// A proven KnownPresent owner replacement on the DEFAULT (no-inherit) path
// drops the durable relationship without arming release-before-reacquire, and
// the caller must then NOT re-bind the replacement with a Grip that never left
// the new weapon's grab zone. The hold-mode acquisition edge is level-triggered
// (`held && inZone`), and through a weapon swap the player's support hand is
// usually still inside the NEW weapon's zone, so in the installed build the
// drop was followed by a re-bind 38-51 ms later (13:14:48.377 -> .415,
// 13:14:52.320 -> .359, 13:16:34.978 -> 35.029, 13:19:01.063 -> 01.100): the
// switched weapon arrived pre-held. The corrected product statement is that the
// default switch does not pre-hold; it engages when the grab point leaves the
// zone and comes back - a fresh crossing, with no Grip release/press cycle.
//
// This predicate is the only transition that arms that requirement: the
// no-inherit stage-2 owner break. A proven absence and a generation/lifecycle
// replacement arm release-before-reacquire instead, the inherit option rebinds
// in place (`DurableWriterAction::Bind`) and never reaches the drop, a
// role/gesture teardown answers at the caller's lifecycle gate first, and
// `switchInherit` keeps that option's shipped semantics untouched.
//
// `evidenceIsDifferentOwner` is the caller's `evidence == KnownPresent &&
// !SameOwner(evidenceOwner, previousOwner)`; `lifecycleIdentityLost` is the
// caller's `DurableRelationshipIdentityLost` result, which already covers the
// generation replacement that stage 0 answers before stage 2 is reached.
constexpr bool DefaultSwitchReplacementDrop(bool engagedBefore,
    DurableWriterAction action, bool evidenceIsDifferentOwner,
    bool lifecycleIdentityLost, bool switchInherit) noexcept
{
    return engagedBefore && action == DurableWriterAction::Invalidated &&
        evidenceIsDifferentOwner && !lifecycleIdentityLost && !switchInherit;
}

// One update of the crossing requirement. The caller owns the flag (vr.cpp's
// PersistentSupportGripState::awaitZoneReentry) and applies both results:
//   * the requirement survives an update only while the zone reading stays
//     TRUE. The first FALSE reading is the fresh crossing it waits for - the
//     grab point has left the zone - so the requirement is satisfied and the
//     next held/in-zone update may acquire;
//   * `holdAcquisitionEdge` is the hold-mode acquisition edge, allowed only
//     with the requirement already clear.
// Toggle mode deliberately does not consume `holdAcquisitionEdge`: a fresh
// press is already a deliberate acquisition, so toggle semantics are unchanged.
struct SwitchZoneReentryStep
{
    bool awaitZoneReentry = false;
    bool holdAcquisitionEdge = false;
};

constexpr SwitchZoneReentryStep StepSwitchZoneReentry(bool awaitZoneReentry,
    bool gripHeld, bool inZone) noexcept
{
    SwitchZoneReentryStep step{};
    step.awaitZoneReentry = awaitZoneReentry && inZone;
    step.holdAcquisitionEdge = gripHeld && inZone && !awaitZoneReentry;
    return step;
}

// ---------------------------------------------------------------------------
// F02 lifecycle identity: what the relationship may survive
// ---------------------------------------------------------------------------

// The lifecycle a durable relationship may still belong to. The relationship
// survives Paused (product policy retains across pause) and a temporary
// tracking/evidence outage, which is not a lifecycle change at all. It cannot
// survive a lifecycle that proves the bound owner is not the current gameplay
// owner: no active title, an unclassified title, an unavailable (zero) title
// generation, or a runtime mode that cannot hold a gameplay weapon (Shell,
// Loading, Dead).
//
// RuntimeMode::Unsupported is deliberately NOT a termination here. Titles
// publish it for ambiguous non-gameplay states (a front end, a paused scripted
// sequence, a state the title itself cannot classify) where "the previous
// weapon cannot remain a valid owner" is not proven; the owner-break and
// evidence rules below still cover a real weapon change, and treating an
// ambiguity as proof of absence would violate the same principle that makes
// Unknown evidence non-durable.
inline bool DurableLifecycleIdentifiesOwner(GameTitle activeTitle,
    uint32_t activeGeneration, RuntimeMode mode) noexcept
{
    if (!TitleIdentifiesOwner(activeTitle)) return false;
    if (activeGeneration == 0) return false;
    return mode != RuntimeMode::Shell && mode != RuntimeMode::Loading &&
        mode != RuntimeMode::Dead;
}

// An ENGAGED relationship additionally terminates when its own identity is no
// longer the active lifecycle's: another title owns the frame, or the title's
// generation was replaced (either side unavailable counts as replaced, so a
// generation that drops to zero mid-relationship terminates it). The caller
// arms the release-before-reacquire policy BEFORE the writer clears anything.
inline bool DurableRelationshipIdentityLost(GameTitle activeTitle,
    uint32_t activeGeneration, RuntimeMode mode,
    const OwnerTuple& owner) noexcept
{
    if (owner.title != activeTitle) return true;
    if (owner.generation != activeGeneration) return true;
    return !DurableLifecycleIdentifiesOwner(activeTitle, activeGeneration, mode);
}

// ---------------------------------------------------------------------------
// F08 freshness: owner-evidence publication revisions
// ---------------------------------------------------------------------------

// Every owner-evidence publication carries a monotonic producer revision. A
// fresh acquisition intent records the revision it observed as a floor, and
// may bind only from a KnownPresent published strictly after that floor, so a
// cached previous-owner record can never create a new relationship. This
// costs at most one evidence-production cycle and never guesses an owner.
constexpr bool EvidenceRevisionFresh(uint64_t publicationRevision,
    uint64_t revisionFloor) noexcept
{
    return publicationRevision > revisionFloor;
}

// F12: pending acquisition intent. The ordinary admission path requests a
// relationship; when the current owner cannot be proven yet, that request must
// not be dropped because the physical button came up (toggle mode), nor may it
// outlive the hold (hold mode). This is an intent, NOT a second relationship
// latch: it carries no owner, and any lifecycle, title, generation, role or
// secondary-weapon invalidation cancels it.
struct PendingAcquisitionIntent
{
    bool pending = false;
    GameTitle title = GameTitle::None;
    uint32_t generation = 0;
    uint64_t evidenceFloor = 0;
    bool holdMode = false;
};

enum class PendingAcquisitionAction : uint8_t
{
    None = 0,
    Begun = 1,     // an acquisition edge began a new pending intent
    Cancelled = 2, // explicit cancel or lifecycle/title/generation replacement
    Elapsed = 3,   // hold mode: the physical Grip was released
};

struct PendingAcquisitionInputs
{
    bool engaged = false;          // a durable relationship already exists
    bool acquisitionEdge = false;  // this update's rising acquisition request
    bool cancelRequested = false;  // explicit second toggle / user cancel
    bool lifecycleValid = true;    // title/generation/role/secondary valid
    bool holdMode = false;         // configured engage style (false = toggle)
    bool gripHeld = false;         // physical Grip state
    GameTitle title = GameTitle::None;
    uint32_t generation = 0;
    uint64_t publicationRevision = 0; // latest published evidence revision
};

// One update of the pending-acquisition policy. Toggle-mode intents survive a
// button-up; hold-mode intents end when the Grip is released. A title or
// generation replacement, a role/secondary/lifecycle invalidation, an explicit
// cancel, or an already-engaged relationship ends the intent.
inline PendingAcquisitionAction PendingAcquisitionStep(
    PendingAcquisitionIntent& intent,
    const PendingAcquisitionInputs& inputs) noexcept
{
    if (intent.pending)
    {
        const bool titleReplaced =
            inputs.title != GameTitle::None &&
            intent.title != GameTitle::None && inputs.title != intent.title;
        const bool generationReplaced =
            inputs.generation != 0 && intent.generation != 0 &&
            inputs.generation != intent.generation;
        if (inputs.engaged || !inputs.lifecycleValid || titleReplaced ||
            generationReplaced)
        {
            intent = PendingAcquisitionIntent{};
            return PendingAcquisitionAction::Cancelled;
        }
        if (inputs.cancelRequested)
        {
            intent = PendingAcquisitionIntent{};
            return PendingAcquisitionAction::Cancelled;
        }
        if (intent.holdMode && !inputs.gripHeld)
        {
            intent = PendingAcquisitionIntent{};
            return PendingAcquisitionAction::Elapsed;
        }
        return PendingAcquisitionAction::None;
    }
    if (!inputs.acquisitionEdge || inputs.engaged || !inputs.lifecycleValid ||
        inputs.cancelRequested)
        return PendingAcquisitionAction::None;
    intent.pending = true;
    intent.title = inputs.title;
    intent.generation = inputs.generation;
    intent.holdMode = inputs.holdMode;
    intent.evidenceFloor = inputs.publicationRevision;
    return PendingAcquisitionAction::Begun;
}

// A pending intent may bind only from a KnownPresent that (a) is causally newer
// than the evidence the intent began from and (b) still belongs to the same
// lifecycle identity the intent recorded. Everything else returns false, so
// the durable writer waits instead of guessing an owner.
inline bool PendingAcquisitionBindable(const PendingAcquisitionIntent& intent,
    GameTitle title, uint32_t generation, OwnerEvidence evidence,
    uint64_t publicationRevision) noexcept
{
    return intent.pending && evidence == OwnerEvidence::KnownPresent &&
        (intent.title == GameTitle::None || intent.title == title) &&
        (intent.generation == 0 || intent.generation == generation) &&
        EvidenceRevisionFresh(publicationRevision, intent.evidenceFloor);
}

// The acquisition request the durable writer consumes for a fresh bind. The
// pending intent IS the request once spatial admission has succeeded (it
// survives the toggle button-up), and the fresh-owner proof gates it, so a
// bind can never come from stale prior-owner evidence.
inline bool PendingAcquisitionRequest(const PendingAcquisitionIntent& intent,
    GameTitle title, uint32_t generation, OwnerEvidence evidence,
    uint64_t publicationRevision) noexcept
{
    return PendingAcquisitionBindable(intent, title, generation, evidence,
        publicationRevision);
}

// Current-invocation trust. Mismatch, instability or invocation-local Unknown
// all fail closed for this invocation without mutating the durable
// relationship.
inline bool InvocationOwnerTrusted(
    bool engaged, const OwnerTuple& relationshipOwner,
    OwnerEvidence invocationEvidence, const OwnerTuple& invocationOwner) noexcept
{
    return engaged && invocationEvidence == OwnerEvidence::KnownPresent &&
        OwnerComplete(invocationOwner) &&
        SameOwner(relationshipOwner, invocationOwner);
}

// Agreement-aware current-invocation trust for titles whose reader exposes the
// FP producer/slot agreement bit (H3/ODST/Reach/H4). During an owner swap the
// inventory-side read can still name the previous unit while the FP record
// already names the new weapon, so a single-read KnownPresent is not enough:
// the producer must also confirm it assembled the same slot. Every failure
// (agreement clear, Unknown/Absent evidence, incomplete or mismatched tuple)
// fails closed for this invocation.
inline bool InvocationOwnerTrusted(
    bool engaged, const OwnerTuple& relationshipOwner,
    OwnerEvidence invocationEvidence, bool producerAgreement,
    const OwnerTuple& invocationOwner) noexcept
{
    return engaged && producerAgreement &&
        invocationEvidence == OwnerEvidence::KnownPresent &&
        OwnerComplete(invocationOwner) &&
        SameOwner(relationshipOwner, invocationOwner);
}

// Solve-time support qualification (F14). One aim assembly resolves the
// durable relationship ONCE, before any support-capable raw aim is selected,
// and freezes the result as the receipt that travels with the produced pose:
//
//   * an unreadable/incoherent relationship is never proof of disengagement:
//     the assembly takes the ordinary one-hand path and the receipt must not
//     report epoch 0, because every consumer reads epoch 0 as "the
//     relationship was coherently readable and disengaged";
//   * a coherently disengaged relationship is the ordinary one-hand case
//     (epoch 0, no forcing);
//   * an engaged relationship may consume support geometry only when this
//     invocation proved the same owner; otherwise it is denied for this
//     invocation but keeps the engaged epoch, so a consumer can never mistake
//     a denied invocation for an ordinary one-hand ray.
//
// `kUnknownRelationshipEpoch` is the "the relationship could not be read"
// marker for a receipt: deliberately nonzero.
inline constexpr uint64_t kUnknownRelationshipEpoch = ~uint64_t{0};

struct SolveSupportQualification
{
    // The assembly must resolve to the ordinary calibrated one-hand path.
    bool forceOneHand = false;
    // Support-capable two-hand geometry may be consumed (this invocation
    // proved the relationship owner); the solve still decides whether the
    // geometry actually steered the produced pose.
    bool supportTrusted = false;
    // 0 only for a coherently disengaged read. An engaged epoch is carried
    // through whether the invocation is trusted or denied; an unreadable
    // relationship carries kUnknownRelationshipEpoch.
    uint64_t supportEpoch = 0;
    bool relationshipReadable = false;
    bool relationshipEngaged = false;
};

inline SolveSupportQualification QualifySolveSupport(bool featureEnabled,
    bool relationshipReadable, bool relationshipEngaged,
    uint64_t relationshipEpoch, bool invocationUntrusted) noexcept
{
    SolveSupportQualification qualification{};
    if (!featureEnabled)
        return qualification; // feature off: no read, no forcing, no receipt
    qualification.relationshipReadable = relationshipReadable;
    qualification.relationshipEngaged = relationshipEngaged;
    if (!relationshipReadable ||
        (relationshipEngaged && relationshipEpoch == 0))
    {
        // F14: unavailable/incoherent (or an engaged read carrying an
        // impossible zero epoch) is not a disengaged read.
        qualification.forceOneHand = true;
        qualification.supportEpoch = kUnknownRelationshipEpoch;
        qualification.relationshipEngaged = false;
        return qualification;
    }
    if (!relationshipEngaged)
        return qualification; // coherently disengaged: ordinary one-hand
    qualification.supportEpoch = relationshipEpoch;
    if (invocationUntrusted)
    {
        qualification.forceOneHand = true;
        return qualification; // engaged epoch retained; trusted stays false
    }
    qualification.supportTrusted = true;
    return qualification;
}

// Live title consumers outside an exact packet transaction: a support
// carrier may be used only when no durable relationship is engaged or the
// CURRENT invocation proves the relationship owner. Mismatch, absence,
// read failure and Unknown all detach to the independent one-hand carrier.
// Never mutates the durable relationship.
inline bool SupportCarrierMustDetachForInvocation(bool engaged,
    const OwnerTuple& relationshipOwner, OwnerEvidence currentEvidence,
    const OwnerTuple& currentOwner) noexcept
{
    if (!engaged) return false;
    return !InvocationOwnerTrusted(engaged, relationshipOwner,
        currentEvidence, currentOwner);
}

// Exact-invocation carrier decision shared by the CE and Halo 2 consumers.
// relationshipReadable=false means continuity cannot be proven: support
// geometry is never consumed for this invocation. A readable DISENGAGED
// relationship detaches only when the frozen publication still carries
// support-derived geometry (the release discontinuity); an ordinary one-hand
// publication stays valid. An engaged relationship follows the invocation's
// own owner proof. Never mutates the durable relationship.
inline bool SupportInvocationMustDetach(bool relationshipReadable,
    bool relationshipEngaged, bool ownerTrusted,
    bool publicationSupportDerived) noexcept
{
    if (!relationshipReadable) return true;
    if (!relationshipEngaged) return publicationSupportDerived;
    return !ownerTrusted;
}

// Provenance of a frozen aim solve: true iff the solve that produced this
// exact pose accepted support-derived two-hand geometry. Derived only from
// the frozen AimPoseResult, never from live latch/relationship state.
constexpr bool AimSupportDerived(bool aimValid, bool aimTwoHandActive) noexcept
{
    return aimValid && aimTwoHandActive;
}

// A completed support-derived ray is consumable only when the ray itself
// carries trusted same-epoch support and the consuming invocation's current
// owner is proven identical (producer agreement required where the reader
// exposes it). This is the single owner rule used at publication and again at
// every consumer that holds its own current-owner evidence.
//
// F14 repair: epoch 0 is no longer a free pass. An epoch-0 ray is accepted only
// when the caller asserts `relationshipDisengagedCoherent` - the relationship
// state was coherently readable AND disengaged when the reading was taken. The
// latch-engaged/publication-old race, an unavailable snapshot and an
// unreadable-but-"no relationship" reach all fail this test, exactly like
// PresentedRayOwnerCompatible.
inline bool SupportRayConsumable(uint64_t raySupportEpoch,
    bool raySupportTrusted,
    bool liveEngaged, uint64_t liveEpoch,
    bool relationshipDisengagedCoherent,
    OwnerEvidence currentEvidence, bool currentAgreement,
    const OwnerTuple& currentOwner, const OwnerTuple& liveOwner) noexcept
{
    if (raySupportEpoch == 0)
        return !liveEngaged && relationshipDisengagedCoherent;
    return raySupportTrusted && liveEngaged && liveEpoch == raySupportEpoch &&
        InvocationOwnerTrusted(liveEngaged, liveOwner, currentEvidence,
            currentAgreement, currentOwner);
}

// The visible support hand follows the valid relationship for this invocation,
// not the aim-authority result. `aimActive` stays a separate flag.
constexpr bool SupportPresentationAttached(bool engaged, bool trusted) noexcept
{
    return engaged && trusted;
}

// Two-producer stability for one invocation (the read before and the read
// after the native original). A present owner must be proven by BOTH reads,
// and when the caller requires it, both reads must also carry producer
// agreement. Absence is complete when both reads prove it and deliberately
// does NOT require agreement: the native readers return before setting the
// FP-agreement bit when the primary slot is empty, so requiring it would make
// KnownAbsent unreachable and leave a proven absence permanently Unknown.
// Any mixed present/absent pair is Unknown -- never proof of absence.
inline OwnerEvidence ResolveStableEvidence(bool requireProducerAgreement,
    uint8_t beforeState, bool beforeAgreement,
    uint8_t afterState, bool afterAgreement) noexcept
{
    if (beforeState == static_cast<uint8_t>(OwnerEvidence::KnownPresent) &&
        afterState == static_cast<uint8_t>(OwnerEvidence::KnownPresent) &&
        (!requireProducerAgreement || (beforeAgreement && afterAgreement)))
        return OwnerEvidence::KnownPresent;
    if (beforeState == static_cast<uint8_t>(OwnerEvidence::KnownAbsent) &&
        afterState == static_cast<uint8_t>(OwnerEvidence::KnownAbsent))
        return OwnerEvidence::KnownAbsent;
    return OwnerEvidence::Unknown;
}

// CE owner-evidence mapping. Only a full raw-slot absence (a valid FP user
// record whose raw weapon slot is empty) is KnownAbsent; a non-null candidate
// that failed object/owner validation, or a missing user record, is Unknown.
inline OwnerEvidence CeOwnerEvidence(bool readOk, bool hasUserRecord,
    bool rawSlotPresent, uint32_t weapon) noexcept
{
    if (!readOk)
        return OwnerEvidence::Unknown;
    if (weapon != 0xFFFFFFFFu)
        return OwnerEvidence::KnownPresent;
    return (hasUserRecord && !rawSlotPresent)
        ? OwnerEvidence::KnownAbsent
        : OwnerEvidence::Unknown;
}

// A completed presented ray is owner-compatible only when it either carries no
// support data at all (an ordinary one-hand ray) or was produced under trusted
// same-owner state and the durable relationship is still that exact epoch.
// Matching the durable epoch alone is deliberately not sufficient: the ray
// must also carry the trusted-owner proof from its producing invocation.
//
// F14: an epoch-0 ray is accepted only when the caller explicitly asserts
// `relationshipDisengagedCoherent` -- that the relationship state was
// coherently readable AND disengaged when the pose was solved. The latch-
// engaged/publication-old race (a support-derived pose published while the
// relationship snapshot still reads disengaged) and an unavailable snapshot
// both fail this test, so the caller must force one-hand / refuse the ray.
inline bool PresentedRayOwnerCompatible(
    bool relationshipEngaged, uint64_t relationshipEpoch,
    uint64_t raySupportEpoch, bool raySupportTrusted,
    bool relationshipDisengagedCoherent) noexcept
{
    if (raySupportEpoch == 0)
        return !relationshipEngaged && relationshipDisengagedCoherent;
    return relationshipEngaged && raySupportTrusted &&
        relationshipEpoch == raySupportEpoch;
}

// Presented-ray freshness bound. The compositor publishes the visible ray
// during composition of the frame it just rendered, so a title-side consumer
// running before the next composition can see a ray carrying the current or
// the immediately previous prepared-frame serial. Anything older is stale.
// This bounds staleness ONLY: it does not prove the ray's owner (that is the
// epoch/trust check above) and it does not close the residual one-frame owner
// race between the ray's production and the consumer's read.
inline bool PresentedRaySerialCompatible(
    uint64_t raySerial, uint64_t currentSerial) noexcept
{
    return raySerial != 0 && currentSerial != 0 &&
        raySerial <= currentSerial && currentSerial - raySerial <= 1;
}

// Owner-qualified reticle smoothing continuity. The presented support history
// is discontinuous when the owner is not safe for this consuming invocation
// or the durable relationship epoch changed; either forces an unsmoothed
// reseed. Aim authority is deliberately not an input.
inline bool ReticleSmoothingDiscontinuity(bool ownerSafe,
    uint64_t supportEpoch, uint64_t lastSupportEpoch) noexcept
{
    return !ownerSafe || supportEpoch != lastSupportEpoch;
}

// ---------------------------------------------------------------------------
// F06/F07/F14: title-native invocation receipt and presented-ray consumption
// ---------------------------------------------------------------------------

// The title's own current-invocation owner proof, frozen once per FP
// interpolate invocation and published for the presented-ray consumers of the
// same prepared frame. The producer never re-reads live durable evidence at a
// consumer, and a consumer never has to trust the latest durable cache: the
// receipt carries the evidence the title actually observed (with its producer
// agreement bit), the owner tuple it resolved, and the relationship reading
// taken at the same instant. A consumer re-qualifies that receipt against the
// LIVE relationship, so a later owner/epoch change invalidates it.
struct SupportInvocationReceipt
{
    bool resolved = false;
    GameTitle title = GameTitle::None;
    uint32_t generation = 0;
    uint64_t serial = 0; // prepared serial the receipt belongs to
    OwnerEvidence evidence = OwnerEvidence::Unknown;
    bool producerAgreement = false;
    OwnerTuple owner{};
    bool relationshipReadable = false;
    bool relationshipEngaged = false;
    uint64_t relationshipEpoch = 0;
};

// A receipt may qualify a consuming frame only when it was actually resolved
// for the same title generation and is no more than one prepared serial older
// than that frame - the same staleness bound the presented ray itself obeys.
// Blind serial equality is deliberately not required: the title's invocation
// for a completed ray can legitimately be one serial old.
inline bool InvocationReceiptCoversConsumer(GameTitle title,
    const SupportInvocationReceipt& receipt, uint32_t consumerGeneration,
    uint64_t consumerSerial) noexcept
{
    return receipt.resolved && receipt.title == title &&
        receipt.generation != 0 && receipt.generation == consumerGeneration &&
        PresentedRaySerialCompatible(receipt.serial, consumerSerial);
}

// F06 consumer qualification. A support-sensitive title path may consume a
// completed presented ray only when BOTH halves hold:
//
//   (a) the producer receipt published with the ray is compatible with the
//       durable relationship read at consumption time - same owner epoch, the
//       ray carries trusted support, and the F14 epoch-0 coherence discipline
//       holds - and the ray is inside the serial staleness bound; and
//   (b) the consuming invocation's own frozen receipt covers this frame AND
//       proves the same owner against the LIVE relationship (producer
//       agreement where the title exposes it).
//
// Matching the durable epoch alone is never sufficient: the F06 counterexample
// is a ray produced under trusted A/epoch 42 that is still presented while the
// current invocation already resolves owner B and the durable relationship has
// not yet been invalidated. Clause (b) refuses it, so the caller keeps its
// existing stock/native behaviour instead of injecting a stale A ray.
inline bool PresentedRayConsumableForInvocation(GameTitle title,
    const SupportInvocationReceipt& invocation,
    uint32_t consumerGeneration, uint64_t consumerSerial,
    bool relationshipReadable, bool relationshipEngaged,
    uint64_t relationshipEpoch, const OwnerTuple& relationshipOwner,
    uint64_t raySupportEpoch, bool raySupportTrusted,
    uint64_t raySerial) noexcept
{
    if (!InvocationReceiptCoversConsumer(title, invocation, consumerGeneration,
            consumerSerial))
        return false;
    if (!PresentedRaySerialCompatible(raySerial, consumerSerial))
        return false;
    if (!PresentedRayOwnerCompatible(relationshipEngaged, relationshipEpoch,
            raySupportEpoch, raySupportTrusted,
            relationshipReadable && !relationshipEngaged))
        return false;
    return SupportRayConsumable(raySupportEpoch, raySupportTrusted,
        relationshipEngaged, relationshipEpoch,
        relationshipReadable && !relationshipEngaged,
        invocation.evidence, invocation.producerAgreement,
        invocation.owner, relationshipOwner);
}

} // namespace support_grip
